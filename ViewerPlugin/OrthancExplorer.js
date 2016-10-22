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
