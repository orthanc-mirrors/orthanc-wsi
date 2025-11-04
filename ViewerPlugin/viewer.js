/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2025 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2025 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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


function InitializePyramid(pyramid, tilesBaseUrl)
{
  $('#map').css('background', pyramid['BackgroundColor']);  // New in WSI 2.1

  var width = pyramid['TotalWidth'];
  var height = pyramid['TotalHeight'];
  var countLevels = pyramid['Resolutions'].length;

  var metersPerUnit = null;
  var imagedVolumeWidth = pyramid['ImagedVolumeWidth'];  // In millimeters
  var imagedVolumeHeight = pyramid['ImagedVolumeHeight'];
  if (imagedVolumeWidth !== undefined &&
      imagedVolumeHeight !== undefined) {
    metersPerUnit = parseFloat(imagedVolumeWidth) / (1000.0 * parseFloat(height));
    //metersPerUnit = parseFloat(imagedVolumeHeight) / (1000.0 * parseFloat(width));
  }

  // Maps always need a projection, but Zoomify layers are not geo-referenced, and
  // are only measured in pixels.  So, we create a fake projection that the map
  // can use to properly display the layer.
  var proj = new ol.proj.Projection({
    code: 'pixel',
    units: 'pixel',
    metersPerUnit: metersPerUnit,
    extent: [0, 0, width, height]
  });

  var extent = [0, -height, width, 0];

  var rotateControl = new ol.control.Rotate({
    autoHide: false,  // Show the button even if rotation is 0
    resetNorth: function() {  // Disable the default action
    }
  });

  new bootstrap.Popover(rotateControl.element, {
    placement: 'right',
    container: 'body',
    html: true,
    content: $('#popover-content')
  });

  // Disable the rotation of the map, and inertia while panning
  // http://stackoverflow.com/a/25682186
  var interactions = ol.interaction.defaults.defaults({
    //pinchRotate : false,
    dragPan: false  // disable kinetics
    //shiftDragZoom: false  // disable zoom box
  }).extend([
    new ol.interaction.DragPan(),
    new ol.interaction.DragRotate({
      //condition: ol.events.condition.shiftKeyOnly  // Rotate only when Shift key is pressed
    })
  ]);

  var controls = ol.control.defaults.defaults({
    attribution: false
  }).extend([
    rotateControl,
    new ol.control.ScaleLine({
      minWidth: 100
    })
  ]);

  const params = new URLSearchParams(document.location.search);
  if (params.has('description')) {
    controls.extend([
      new ol.control.Attribution({
        attributions: params.get('description'),
        collapsible: false
      })
    ]);
  }


  var layer = new ol.layer.Tile({
    extent: extent,
    source: new ol.source.TileImage({
      projection: proj,
      tileUrlFunction: function(tileCoord, pixelRatio, projection) {
        return (tilesBaseUrl + (countLevels - 1 - tileCoord[0]) + '/' + tileCoord[1] + '/' + tileCoord[2]);
      },
      tileGrid: new ol.tilegrid.TileGrid({
        extent: extent,
        resolutions: pyramid['Resolutions'].reverse(),
        tileSizes: pyramid['TilesSizes'].reverse()
      })
    }),
    wrapX: false,
    projection: proj
  });


  var map = new ol.Map({
    target: 'map',
    layers: [ layer ],
    view: new ol.View({
      projection: proj,
      center: [width / 2, -height / 2],
      zoom: 0,
      minResolution: 0.1   // "1" means "do not interpelate over pixels"
    }),
    interactions: interactions,
    controls: controls
  });

  map.getView().fit(extent, map.getSize());


  $('#rotation-slider').on('input change', function() {
    map.getView().setRotation(this.value / 180 * Math.PI);
  });

  $('#rotation-reset').click(function() {
    $('#rotation-slider').val(0).change();
  });

  $('#rotation-minus90').click(function() {
    var angle = parseInt($('#rotation-slider').val()) - 90;
    if (angle < -180) {
      angle += 360;
    }
    $('#rotation-slider').val(angle).change();
  });

  $('#rotation-plus90').click(function() {
    var angle = parseInt($('#rotation-slider').val()) + 90;
    if (angle > 180) {
      angle -= 360;
    }
    $('#rotation-slider').val(angle).change();
  });
}


$(document).ready(function() {
  const params = new URLSearchParams(document.location.search);

  if (params.has('series')) {
    var seriesId = params.get('series');
    $.ajax({
      url : '../pyramids/' + seriesId,
      error: function() {
        alert('Error - Cannot get the pyramid structure of series: ' + seriesId);
      },
      success : function(pyramid) {
        InitializePyramid(pyramid, '../tiles/' + seriesId + '/');
      }
    });
  } else if (params.has('instance')) {
    var frameNumber = 0;
    if (params.has('frame')) {
      frameNumber = params.get('frame');
    }

    var instanceId = params.get('instance');
    $.ajax({
      url : '../frames-pyramids/' + instanceId + '/' + frameNumber,
      error: function() {
        alert('Error - Cannot get the pyramid structure of frame ' + frameNumber + ' of instance: ' + instanceId);
      },
      success : function(pyramid) {
        InitializePyramid(pyramid, '../frames-tiles/' + instanceId + '/' + frameNumber + '/');
      }
    });
  } else {
    alert('Error - No series ID and no instance ID specified!');
  }
});
