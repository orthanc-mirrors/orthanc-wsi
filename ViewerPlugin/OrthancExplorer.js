/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2024-2024 Orthanc Team SRL, Belgium
 * Copyright (C) 2021-2024 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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


$('#series').live('pagebeforeshow', function() {
  var seriesId = $.mobile.pageData.uuid;

  $('#mirador-button').remove();
  $('#openseadragon-button').remove();
  $('#wsi-button').remove();

  $('#series-iiif-button').remove();
  $('#series-access').listview("refresh");

  // Test whether this is a whole-slide image by check the SOP Class
  // UID of one instance of the series
  GetResource('/series/' + seriesId, function(series) {
    GetResource('/instances/' + series['Instances'][0] + '/tags?simplify', function(instance) {
      console.log(instance['SOPClassUID']);

      if (instance['SOPClassUID'] == '1.2.840.10008.5.1.4.1.1.77.1.6') {

        // This is a whole-slide image, register the button
        var b = $('<a>')
          .attr('id', 'wsi-button')
          .attr('data-role', 'button')
          .attr('href', '#')
          .attr('data-icon', 'search')
          .attr('data-theme', 'e')
          .text('Whole-Slide Imaging Viewer')
          .button();

        b.insertAfter($('#series-info'));
        b.click(function() {
          if ($.mobile.pageData) {
            window.open('../wsi/app/viewer.html?series=' + seriesId);
          }
        });

        if (${SERVE_OPEN_SEADRAGON}) {
          var b = $('<a>')
              .attr('id', 'openseadragon-button')
              .attr('data-role', 'button')
              .attr('href', '#')
              .attr('data-icon', 'search')
              .attr('data-theme', 'e')
              .text('Test IIIF in OpenSeadragon')
              .button();

          b.insertAfter($('#series-info'));
          b.click(function() {
            if ($.mobile.pageData) {
              window.open('../wsi/app/openseadragon.html?image=../iiif/tiles/' + seriesId + '/info.json');
            }
          });
        }
      }

      if (${ENABLE_IIIF}) {
        var b = $('<a>')
            .attr('data-role', 'button')
            .attr('href', '#')
            .text('Copy link to IIIF manifest');

        var li = $('<li>')
            .attr('id', 'series-iiif-button')
            .attr('data-icon', 'gear')
            .append(b);

        $('#series-access').append(li).listview("refresh");

        b.click(function(e) {
          if ($.mobile.pageData) {
            e.preventDefault();
            var url = new URL('../wsi/iiif/series/' + seriesId + '/manifest.json', window.location.href);
            navigator.clipboard.writeText(url.href);
            $(e.target).closest('li').buttonMarkup({ icon: 'check' });
          }
        });
      }

      if (${SERVE_MIRADOR}) {
        var b = $('<a>')
            .attr('id', 'mirador-button')
            .attr('data-role', 'button')
            .attr('href', '#')
            .attr('data-icon', 'search')
            .attr('data-theme', 'e')
            .text('Test IIIF in Mirador')
            .button();

        b.insertAfter($('#series-info'));
        b.click(function() {
          if ($.mobile.pageData) {
            window.open('../wsi/app/mirador.html?iiif-content=../iiif/series/' + seriesId + '/manifest.json');
          }
        });
      }
    });
  });
});


$('#instance').live('pagebeforeshow', function() {
  var instanceId = $.mobile.pageData.uuid;

  $('#wsi-instance-button').remove();

  var b = $('<a>')
    .attr('id', 'wsi-button')
    .attr('data-role', 'button')
    .attr('href', '#')
    .attr('data-icon', 'search')
    .attr('data-theme', 'e')
    .text('On-the-fly pyramid viewer')
    .button();

  b.insertAfter($('#instance-info'));
  b.click(function() {
    if ($.mobile.pageData) {
      window.open('../wsi/app/viewer.html?instance=' + instanceId);
    }
  });
});
