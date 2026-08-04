/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2026 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2026 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
 *
 * This program is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Affero General Public License for more details.
 * 
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 **/


function CreateSerializationObject()
{
  var serialized = {};

  const params = new URLSearchParams(document.location.search);
  if (params.has('project')) {
    serialized['project'] = params.get('project');
  }

  if (params.has('series')) {
    serialized['level'] = 'series';
    serialized['resource'] = params.get('series');
  } else if (params.has('instance')) {
    serialized['level'] = 'instance';
    serialized['resource'] = params.get('instance');
  } else {
    console.error('No series/instance available');
  }

  return serialized;
}


function SerializeFeature(feature)
{
  var type = feature.getGeometry().getType();

  if (type === 'LineString') {
    return {
      'type' : 'polyline',
      'coordinates' : feature.getGeometry().getCoordinates()
    };
  } else if (type === 'Point') {
    return {
      'type' : 'point',
      'coordinates' : feature.getGeometry().getCoordinates()
    };
  } else if (type === 'Circle') {
    return {
      'type' : 'circle',
      'center' : feature.getGeometry().getCenter(),
      'radius' : feature.getGeometry().getRadius()
    };
  } else if (type === 'Polygon') {
    return {
      'type' : 'polygon',
      'coordinates' : feature.getGeometry().getCoordinates()
    };
  } else {
    console.error('Not implemented: ' + type);
    return null;
  }
}


function UnserializeFeature(json)
{
  if (json.type === 'polyline') {
    return new ol.geom.LineString(json['coordinates']);
  } else if (json.type === 'point') {
    return new ol.geom.Point(json['coordinates']);
  } else if (json.type === 'circle') {
    return new ol.geom.Circle(json['center'], json['radius']);
  } else if (json.type === 'polygon') {
    return new ol.geom.Polygon(json['coordinates']);
  } else {
    console.error('Not implemented: ' + json.type);
    return null;
  }
}


function SerializeLayers(source)
{
  var features = [];

  source.getFeatures().forEach((feature, index) => {
    var item = SerializeFeature(feature);

    if (item !== null) {
      item['layer-id'] = feature.get('layer-id');
      features.push(item);
    }
  });

  var serialized = CreateSerializationObject();

  serialized['annotations'] = {
    'layers' : layers,
    'features' : features
  };

  return serialized;
}


var sourceToSerialize = null;
var isPendingChange = false;
var isSaving = false;

function SaveAnnotations()
{
  function BeforeUnloadHandler(event)
  {
    event.preventDefault();

    // Included for legacy support, e.g. Chrome/Edge < 119
    event.returnValue = true;
  };

  function Execute()
  {
    console.assert(sourceToSerialize !== null);

    var serialized = SerializeLayers(sourceToSerialize);

    isPendingChange = false;
    isSaving = true;
    $('#toolbar-spinner').show();
    window.addEventListener('beforeunload', BeforeUnloadHandler);

    $.ajax({
      type : 'POST',
      url : '../api/save-annotations',
      data : JSON.stringify(serialized),
      contentType: 'application/json',
      success: function() {
      },
      error: function() {
        alert('Cannot save the annotations');
      },
      complete : function() {
        console.assert(isSaving === true);

        if (isPendingChange) {
          Execute();
        } else {
          isSaving = false;
          $('#toolbar-spinner').hide();
          window.removeEventListener('beforeunload', BeforeUnloadHandler);
        }
      }
    });
  }

  if (sourceToSerialize !== null) {
    isPendingChange = true;

    if (!isSaving) {
      Execute();
    }
  }
}


function LoadAnnotations(source)
{
  $('#toolbar-spinner').show();

  $.ajax({
    type : 'POST',
    url : '../api/load-annotations',
    data : JSON.stringify(CreateSerializationObject()),
    contentType: 'application/json',
    success: function(data) {
      if (!'layers' in data ||
          !'features' in data ||
          data['layers'].length == 0) {
        return;
      }

      layers = data.layers;
      activeLayerId = data.layers[0].id;

      source.clear();
      for (i = 0; i < data.features.length; i++) {
        var layerId = data.features[i]['layer-id'];

        if (layerId !== undefined) {
          var geometry = UnserializeFeature(data.features[i]);

          if (geometry !== null) {
            var feature = new ol.Feature(geometry);
            feature.set('layer-id', layerId);
            source.addFeature(feature);
          }
        }
      }

      RenderLayersTable();
    },
    error: function() {
      alert('Cannot load the saved annotations');
    },
    complete: function() {
      sourceToSerialize = source;

      $('#toolbar-spinner').hide();

      // Now that the features are loaded, we can install the save callback
      source.on('addfeature', function (e) {
        SaveAnnotations();
      });
      source.on('removefeature', function (e) {
        SaveAnnotations();
      });
    }
  });
}
