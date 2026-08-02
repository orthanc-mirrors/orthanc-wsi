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
  }

  return serialized;
}


function SerializeAnnotations(source)
{
  const features = source.getFeatures();

  var annotations = [];

  features.forEach((feature, index) => {
    var type = feature.getGeometry().getType();

    if (type === 'LineString') {
      annotations.push({
        'type' : 'polyline',
        'coordinates' : feature.getGeometry().getCoordinates()
      });
    } else if (type === 'Point') {
      annotations.push({
        'type' : 'point',
        'coordinates' : feature.getGeometry().getCoordinates()
      });
    }
  });

  var serialized = CreateSerializationObject();
  serialized['annotations'] = annotations;
  return serialized;
}


var pendingSourceToSave = null;
var isSaving = false;

function SaveAnnotations(source)
{
  function BeforeUnloadHandler(event)
  {
    event.preventDefault();

    // Included for legacy support, e.g. Chrome/Edge < 119
    event.returnValue = true;
  };

  function Execute()
  {
    console.assert(pendingSourceToSave !== null);

    var serialized = SerializeAnnotations(pendingSourceToSave);
    pendingSourceToSave = null;

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

        if (pendingSourceToSave === null) {
          isSaving = false;
          $('#toolbar-spinner').hide();
          window.removeEventListener('beforeunload', BeforeUnloadHandler);
        } else {
          Execute();
        }
      }
    });
  }

  pendingSourceToSave = source;

  if (!isSaving) {
    Execute();
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
    success: function(annotations) {
      for (i = 0; i < annotations.length; i++) {
        var geometry = null;

        if (annotations[i].type === 'polyline') {
          geometry = new ol.geom.LineString(annotations[i]['coordinates']);
        } else if (annotations[i].type === 'point') {
          geometry = new ol.geom.Point(annotations[i]['coordinates']);
        }

        if (geometry !== null) {
          source.addFeature(new ol.Feature(geometry));
        }
      }
    },
    error: function() {
      alert('Cannot load the saved annotations');
    },
    complete: function() {
      $('#toolbar-spinner').hide();

      // Now that the features are loaded, we can install the save callback
      source.on('addfeature', function (e) {
        SaveAnnotations(e.target);
      });
      source.on('removefeature', function (e) {
        SaveAnnotations(e.target);
      });
    }
  });
}
