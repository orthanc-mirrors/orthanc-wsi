/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017 Osimis, Belgium
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


// For IE compatibility
if (!window.console) window.console = {};
if (!window.console.log) window.console.log = function () { };


// http://stackoverflow.com/a/21903119/881731
function GetUrlParameter(sParam) 
{
  var sPageURL = decodeURIComponent(window.location.search.substring(1));
  var sURLVariables = sPageURL.split('&');
  var sParameterName;
  var i;

  for (i = 0; i < sURLVariables.length; i++) 
  {
    sParameterName = sURLVariables[i].split('=');

    if (sParameterName[0] === sParam) 
    {
      return sParameterName[1] === undefined ? '' : sParameterName[1];
    }
  }

  return '';
};



$(document).ready(function() {
  var seriesId = GetUrlParameter('series');
  if (seriesId.length == 0)
  {
    alert('Error - No series ID specified!');
  }
  else
  {
    $.ajax({
      url : '../pyramids/' + seriesId,
      error: function() {
        alert('Error - Cannot get the pyramid structure of series: ' + seriesId);
      },
      success : function(series) {
        var width = series['TotalWidth'];
        var height = series['TotalHeight'];
        var tileWidth = series['TileWidth'];
        var tileHeight = series['TileHeight'];
        var countLevels = series['Resolutions'].length;

        // Maps always need a projection, but Zoomify layers are not geo-referenced, and
        // are only measured in pixels.  So, we create a fake projection that the map
        // can use to properly display the layer.
        var proj = new ol.proj.Projection({
          code: 'pixel',
          units: 'pixels',
          extent: [0, 0, width, height]
        });

        var extent = [0, -height, width, 0];
        
        // Disable the rotation of the map, and inertia while panning
        // http://stackoverflow.com/a/25682186
        var interactions = ol.interaction.defaults({
          altShiftDragRotate : false, 
          pinchRotate : false,
          dragPan: false
        }).extend([
          new ol.interaction.DragPan({kinetic: false})
        ]);

        var layer = new ol.layer.Tile({
          extent: extent,
          source: new ol.source.TileImage({
            projection: proj,
            tileUrlFunction: function(tileCoord, pixelRatio, projection) {
              return ('../tiles/' + seriesId + '/' + 
                      (countLevels - 1 - tileCoord[0]) + '/' + tileCoord[1] + '/' + (-tileCoord[2] - 1));
            },
            tileGrid: new ol.tilegrid.TileGrid({
              extent: extent,
              resolutions: series['Resolutions'].reverse(),
              tileSize: [tileWidth, tileHeight]
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
            minResolution: 1   // Do not interpelate over pixels
          }),
          interactions: interactions
        });

        map.getView().fit(extent, map.getSize());
      }
    });
  }
});
