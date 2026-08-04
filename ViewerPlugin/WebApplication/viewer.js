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


function IsNear(a, b)
{
  return Math.abs(a - b) <= 0.01;
}


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
    var metersPerUnitX = parseFloat(imagedVolumeWidth) / (1000.0 * parseFloat(width));
    var metersPerUnitY = parseFloat(imagedVolumeHeight) / (1000.0 * parseFloat(height));
    if (IsNear(metersPerUnitX / metersPerUnitY, 1)) {
      metersPerUnit = metersPerUnitX;
    } else {
      // Backward compatibility with OrthancWSIDicomizer <= 3.2, where X/Y were swapped
      metersPerUnitX = parseFloat(imagedVolumeWidth) / (1000.0 * parseFloat(height));
      metersPerUnitY = parseFloat(imagedVolumeHeight) / (1000.0 * parseFloat(width));
      if (IsNear(metersPerUnitX / metersPerUnitY, 1)) {
        metersPerUnit = metersPerUnitX;
      } else {
        console.error('Anisotropic pixel spacing (may result from an inconsistency ' +
                      'in the imaged volume size), not showing the scale');
      }
    }
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
    target: 'toolbar-left',
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
    attribution: false,
    rotate: false        // remove the default rotate
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

  // Prevent toolbar pointer events from reaching OL interactions (e.g. Select)
  ['toolbar-left', 'toolbar-top'].forEach(function(id) {
    var el = $('#' + id)[0];
    ['pointerdown', 'pointerup', 'pointermove', 'click'].forEach(function(type) {
      el.addEventListener(type, function(e) { e.stopPropagation(); });
    });
  });

  // Re-append toolbars inside the map viewport so they inherit OL's scaling
  var viewport = map.getViewport();
  viewport.appendChild($('#toolbar-left')[0]);
  viewport.appendChild($('#toolbar-top')[0]);

  map.once('postrender', function() {
    // Match Bootstrap button size to OL button size
    var olBtnSize = $('.ol-zoom button')[0].offsetWidth + 'px';
    $('.icon-btn').each(function() {
      this.style.width = olBtnSize;
      this.style.height = olBtnSize;
    });

    // Move the top toolbar directly right to the zoom control, regardless of scaling
    var zoomEl = $('.ol-zoom')[0];
    $('#toolbar-top')[0].style.left = (zoomEl.offsetLeft + zoomEl.offsetWidth) + 'px';
    $('#toolbar-top')[0].style.top = zoomEl.offsetTop + 'px';

    // Move the left toolbar directly below the zoom control, regardless of scaling
    $('#toolbar-left')[0].style.left = zoomEl.offsetLeft + 'px';
    $('#toolbar-left')[0].style.top = (zoomEl.offsetTop + zoomEl.offsetHeight) + 'px';

    // Move the vertical buttons below the rotate control, regardless of scaling
    var rotateEl = $('.ol-rotate')[0];
    $('#toolbar-left-content')[0].style.top = (rotateEl.offsetTop + rotateEl.offsetHeight) + 'px';

    $('#toolbar-top, #toolbar-left, #right-panel-toggle').css('visibility', '');
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

  InitializeDrawing(map);
}


function InitializePanelAnimation()
{
  // This makes the toggle vertical bar follow the resizing of the right panel
  var panel = $('#right-panel')[0];
  var toggle = $('#right-panel-toggle')[0];
  var icon = $('#right-panel-toggle-icon')[0];
  var isResizing = false;

  function resizingLoop() {
    toggle.style.right = (window.innerWidth - panel.getBoundingClientRect().left) + 'px';
    if (isResizing) {
      requestAnimationFrame(resizingLoop);
    }
  }

  function startResizing() {
    isResizing = true;
    requestAnimationFrame(resizingLoop);
  }

  function stopResizing() {
    isResizing = false;
    resizingLoop();
  }

  resizingLoop();

  // Showing the panel
  panel.addEventListener('hide.bs.offcanvas', function() {
    icon.className = 'bi bi-chevron-compact-left';
    startResizing();
  });
  panel.addEventListener('hidden.bs.offcanvas', stopResizing);

  // Hiding the panel
  panel.addEventListener('show.bs.offcanvas', function() {
    icon.className = 'bi bi-chevron-compact-right';
    startResizing();
  });
  panel.addEventListener('shown.bs.offcanvas', stopResizing);
}


function FormatLength(geometry, projection)
{
  var lengthPx = geometry.getLength();
  var metersPerUnit = projection.getMetersPerUnit();
  if (metersPerUnit) {
    var meters = lengthPx * metersPerUnit;
    if (meters < 1e-3) {
      return (meters * 1e6).toFixed(1) + ' μm';
    } else if (meters < 1) {
      return (meters * 1e3).toFixed(1) + ' mm';
    } else if (meters < 1000) {
      return meters.toFixed(2) + ' m';
    } else {
      return (meters / 1000).toFixed(3) + ' km';
    }
  } else {
    return lengthPx.toFixed(0) + ' px';
  }
}


function AddReadOnlyProperty(label, value)
{
  var row = $(($('#tpl-readonly-property')[0].content.cloneNode(true)).firstElementChild);
  row.find('.prop-label').text(label);
  row.find('.prop-value').text(value);
  $('#annotation-info').append(row);
}


function AddEditableProperty(label, value, onChange)
{
  var row = $(($('#tpl-editable-property')[0].content.cloneNode(true)).firstElementChild);
  row.find('label').text(label);
  var input = row.find('input');
  input.val(value);
  if (onChange) {
    input.on('change', function() { onChange($(this).val()); });
  }
  $('#annotation-info').append(row);
}


function AddDropdownProperty(label, options, selectedValue, onChange)
{
  var row = $(($('#tpl-dropdown-property')[0].content.cloneNode(true)).firstElementChild);
  row.find('label').text(label);
  var select = row.find('select');
  options.forEach(function(opt) {
    $('<option>').val(opt.value).text(opt.label)
                .prop('selected', opt.value === selectedValue)
                .appendTo(select);
  });
  if (onChange) {
    select.on('change', function() { onChange($(this).val()); });
  }
  $('#annotation-info').append(row);
}


function InitializeDrawing(map)
{
  // Vector layer to hold drawn features
  var drawSource = new ol.source.Vector();
  LoadAnnotations(drawSource);


  var drawLayer = new ol.layer.Vector({
    source: drawSource,
    style: new ol.style.Style({
      stroke: new ol.style.Stroke({ color: 'red', width: 2 }),
      image: new ol.style.Circle({
        radius: 5,
        fill: new ol.style.Fill({ color: 'red' })
      })
    })
  });
  map.addLayer(drawLayer);

  // Draw interaction (inactive until toggled)
  var drawLine = new ol.interaction.Draw({
    source: drawSource,
    type: 'LineString'
  });

  // Draw point interaction (inactive until toggled)
  var drawPoint = new ol.interaction.Draw({
    source: drawSource,
    type: 'Point'
  });

  // Draw circle interaction (inactive until toggled)
  var drawCircle = new ol.interaction.Draw({
    source: drawSource,
    type: 'Circle'
  });

  // Draw rectangle interaction (inactive until toggled)
  var drawRectangle = new ol.interaction.Draw({
    source: drawSource,
    type: 'Circle',
    geometryFunction: ol.interaction.Draw.createBox()
  });

  // Draw closed polygon interaction (inactive until toggled)
  var drawClosedPolygon = new ol.interaction.Draw({
    source: drawSource,
    type: 'Polygon'
  });

  // Draw freehand interaction (inactive until toggled)
  var drawFreehand = new ol.interaction.Draw({
    source: drawSource,
    type: 'Polygon',
    freehand: true
  });

  function preventDoubleClickZoom() {
    map.getInteractions().forEach(function(interaction) {
      if (interaction instanceof ol.interaction.DoubleClickZoom) {
        interaction.setActive(false);
        setTimeout(function() { interaction.setActive(true); }, 50);
      }
    });
  }

  drawLine.on('drawend', function(e) {
    preventDoubleClickZoom();
    // Select the new line so it appears blue and shows its length
    selectAnnotation.getFeatures().clear();
    selectAnnotation.getFeatures().push(e.feature);
    selectAnnotation.dispatchEvent({ type: 'select', selected: [e.feature], deselected: [] });
  });
  drawPoint.on('drawend', function(e) {
    preventDoubleClickZoom();
    selectAnnotation.getFeatures().clear();
    selectAnnotation.getFeatures().push(e.feature);
    selectAnnotation.dispatchEvent({ type: 'select', selected: [e.feature], deselected: [] });
  });
  drawCircle.on('drawend', function(e) {
    preventDoubleClickZoom();
    selectAnnotation.getFeatures().clear();
    selectAnnotation.getFeatures().push(e.feature);
    selectAnnotation.dispatchEvent({ type: 'select', selected: [e.feature], deselected: [] });
  });
  drawRectangle.on('drawend', function(e) {
    preventDoubleClickZoom();
    selectAnnotation.getFeatures().clear();
    selectAnnotation.getFeatures().push(e.feature);
    selectAnnotation.dispatchEvent({ type: 'select', selected: [e.feature], deselected: [] });
  });
  drawClosedPolygon.on('drawend', function(e) {
    preventDoubleClickZoom();
    selectAnnotation.getFeatures().clear();
    selectAnnotation.getFeatures().push(e.feature);
    selectAnnotation.dispatchEvent({ type: 'select', selected: [e.feature], deselected: [] });
  });
  drawFreehand.on('drawend', function(e) {
    selectAnnotation.getFeatures().clear();
    selectAnnotation.getFeatures().push(e.feature);
    selectAnnotation.dispatchEvent({ type: 'select', selected: [e.feature], deselected: [] });
  });

  // Select interaction (inactive until toggled)
  var selectAnnotation = new ol.interaction.Select({
    layers: [drawLayer],
    hitTolerance: 5,  /* pixels around the feature that count as a hit */
    style: new ol.style.Style({
      stroke: new ol.style.Stroke({ color: 'blue', width: 3 }),
      image: new ol.style.Circle({
        radius: 5,
        fill: new ol.style.Fill({ color: 'blue' })
      })
    })
  });

  function deactivateAll() {
    map.removeInteraction(drawLine);
    map.removeInteraction(drawPoint);
    map.removeInteraction(drawCircle);
    map.removeInteraction(drawRectangle);
    map.removeInteraction(drawClosedPolygon);
    map.removeInteraction(drawFreehand);
    map.removeInteraction(selectAnnotation);
    $('.icon-btn').removeClass('active');
    map.getViewport().style.cursor = '';
    $('#annotation-info').empty();
  }

  $('#btn-draw-line').on('click', function() {
    var wasActive = $(this).hasClass('active');
    deactivateAll();
    if (!wasActive) {
      map.addInteraction(drawLine);
      map.addInteraction(selectAnnotation);  // kept active to show blue highlight
      $(this).addClass('active');
    }
  });

  $('#btn-draw-point').on('click', function() {
    var wasActive = $(this).hasClass('active');
    deactivateAll();
    if (!wasActive) {
      map.addInteraction(drawPoint);
      map.addInteraction(selectAnnotation);  // kept active to show blue highlight
      $(this).addClass('active');
    }
  });

  $('#btn-draw-circle').on('click', function() {
    var wasActive = $(this).hasClass('active');
    deactivateAll();
    if (!wasActive) {
      map.addInteraction(drawCircle);
      map.addInteraction(selectAnnotation);  // kept active to show blue highlight
      $(this).addClass('active');
    }
  });

  $('#btn-draw-rectangle').on('click', function() {
    var wasActive = $(this).hasClass('active');
    deactivateAll();
    if (!wasActive) {
      map.addInteraction(drawRectangle);
      map.addInteraction(selectAnnotation);  // kept active to show blue highlight
      $(this).addClass('active');
    }
  });

  $('#btn-draw-closed-polygon').on('click', function() {
    var wasActive = $(this).hasClass('active');
    deactivateAll();
    if (!wasActive) {
      map.addInteraction(drawClosedPolygon);
      map.addInteraction(selectAnnotation);  // kept active to show blue highlight
      $(this).addClass('active');
    }
  });

  $('#btn-draw-freehand').on('click', function() {
    var wasActive = $(this).hasClass('active');
    deactivateAll();
    if (!wasActive) {
      map.addInteraction(drawFreehand);
      map.addInteraction(selectAnnotation);  // kept active to show blue highlight
      $(this).addClass('active');
      map.getViewport().style.cursor = 'crosshair';
    }
  });

  selectAnnotation.on('select', function(e) {
    $('#annotation-info').empty();
    if (e.selected.length === 1) {
      var feature = e.selected[0];
      var geometry = feature.getGeometry();
      $('#btn-focus-annotation').show().off('click').on('click', function() {
        bootstrap.Offcanvas.getOrCreateInstance($('#right-panel')[0]).hide();
        map.getView().fit(geometry.getExtent(), { padding: [40, 40, 40, 40], duration: 300 });
      });
      if (geometry.getType() === 'LineString') {
        AddReadOnlyProperty('Length', FormatLength(geometry, map.getView().getProjection()));
      } else {
        // TODO
        AddReadOnlyProperty('Length', '1.23 mm');
        AddReadOnlyProperty('Bounding box', '100 x 200 px');
        AddReadOnlyProperty('Surface', '0.05 mm²');
        AddEditableProperty('Label', feature.get('label') || '', function(v) {
          feature.set('label', v);
        });
        AddDropdownProperty('Category', [
          { value: 'tumor',    label: 'Tumor' },
          { value: 'stroma',   label: 'Stroma' },
          { value: 'necrosis', label: 'Necrosis' }
        ], feature.get('category') || '', function(v) {
          feature.set('category', v);
        });
      }
      bootstrap.Offcanvas.getOrCreateInstance($('#right-panel')[0]).show();
    } else {
      $('#btn-focus-annotation').hide();
    }
  });

  $('#btn-select-annotation').on('click', function() {
    var wasActive = $(this).hasClass('active');
    deactivateAll();
    if (!wasActive) {
      map.addInteraction(selectAnnotation);
      $(this).addClass('active');
      map.getViewport().style.cursor = 'pointer';
    }
  });

  var deleteModal = new bootstrap.Modal($('#modal-delete-annotation')[0]);

  $('#btn-delete-annotation').on('click', function(e) {
    e.stopPropagation();
    var selected = selectAnnotation.getFeatures();
    if (selected.getLength() > 0) {
      deleteModal.show();
    }
  });

  $('#btn-delete-annotation-confirm').on('click', function() {
    var selected = selectAnnotation.getFeatures();
    selected.forEach(function(feature) {
      drawSource.removeFeature(feature);
    });
    selected.clear();
    $('#annotation-info').empty();
    deleteModal.hide();
  });
}


$(document).ready(function() {
  InitializePanelAnimation();

  $('[data-bs-toggle="tooltip"]').each(function() {
    new bootstrap.Tooltip(this, { trigger: 'hover' });
  });

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
