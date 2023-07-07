/**
 * Orthanc - A Lightweight, RESTful DICOM Store
 * Copyright (C) 2012-2016 Sebastien Jodogne, Medical Physics
 * Department, University Hospital of Liege, Belgium
 * Copyright (C) 2017-2023 Osimis S.A., Belgium
 * Copyright (C) 2021-2023 Sebastien Jodogne, ICTEAM UCLouvain, Belgium
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

  $('#wsi-button').remove();

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

      }
    });
  });
});
