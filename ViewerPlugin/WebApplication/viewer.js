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


var app = new Vue({
  el: '#app',
  computed: {
  },

  data() {
    return {
      projectId: '',
      level: '',
      resourceId: '',
      frameNumber: 0,
      brightness: 0, // In the range between [-100,100]
      contrast: 0,   // In the range between [-100,100]
      saturation: 0, // In the range between [-100,100]
      // hue: 0,        // Degrees, in the range between [-180,180]

      // Main state for annotations
      projectName: '',
      projectDescription: '',
      projectUser: null,
      imageDescription: '',
      userLayers: [],
      activeUserLayerId: null,

      // UI state
      alertNotPersistent: false,
      toolbarsVisible: false,
      panelOpen: true,
      mapBackground: '',
      rotationDeg: 0,
      activeDrawTool: null,
      showMagnificationButtons: false,

      // Bootstrap modals
      modalDeleteUserLayer: null,
      modalDeleteAnnotation: null,
      pendingDelete: null,

      // Loading/saving using the backend
      isPendingChange: false,
      isSaving: false,
      showSpinner: false,

      // Annotation selection panel
      annotationProperties: [],
      selectedFeature: null,

      // OpenLayers objects
      map: null,
      drawSource: null,
      drawLayer: null,  // Used in HTML
      drawLine: null,
      drawPoint: null,
      drawCircle: null,
      drawRectangle: null,
      drawClosedPolygon: null,
      drawFreehand: null,
      drawFreehandLine: null,
      moveFeature: null,
      modifyFeature: null,
      selectAnnotation: null,

      /**
       * Magnification at full-resolution image pixels, convention
       * commonly used for pathology WSI: 40x scan = 0.25 µm/pixel
       **/
      referenceMagnification: 40,

      // TODO - Shared layers
      sharedLayersSupported: true,
      sharedSource: null,
      sharedLayer: null,
      modalImportSharedLayer: null,
      sharedLayers: [
        {
          "id": "toto",
          "visible": true,
          "name": "Coucou",
          "color" : "#ff0000",
          "author" : "tata"
        }
      ],
      importAvailableSharedLayers: {},
      importSelectedUser: '',
      importSelectedLayer: '',
      importAvailableLayers: [],
      importUsersLoading: false,
      importUsersFailed: false,
    };
  },

  mounted: function() {
    this.InitializePanelAnimation();

    this.modalDeleteUserLayer = new bootstrap.Modal(document.getElementById('modal-delete-user-layer'));
    this.modalDeleteAnnotation = new bootstrap.Modal(document.getElementById('modal-delete-annotation'));
    this.modalImportSharedLayer = new bootstrap.Modal(document.getElementById('modal-import-shared-layer'));  // TODO

    this.sharedLayersSupported = false;  // TODO

    document.querySelectorAll('[data-bs-toggle="tooltip"]').forEach(function(el) {
      new bootstrap.Tooltip(el, { trigger: 'hover' });
    });

    bootstrap.Offcanvas.getOrCreateInstance(document.getElementById('right-panel')).show();  // TODO - REMOVE

    const params = new URLSearchParams(document.location.search);

    if (params.has('project')) {
      this.projectId = params.get('project');
    }

    if (params.has('description')) {
      this.imageDescription = params.get('description');
    }

    if (params.has('series')) {
      this.level = 'series';
      this.resourceId = params.get('series');
    } else if (params.has('instance')) {
      this.level = 'instance';
      this.resourceId = params.get('instance');

      if (params.has('frame')) {
        this.frameNumber = params.get('frame');
      }
    } else {
      alert('Error - No series ID and no instance ID specified!');
      return;
    }

    this.LoadAnnotationsInfo();
    this.LoadPyramid();
    this.LoadUserLayers();
  },

  methods: {

    // -----------------------------------------------------------------------
    // Persistence of layers and annotations
    // -----------------------------------------------------------------------

    CreatePostPayload: function(args) {
      args['project'] = this.projectId;
      args['level'] = this.level;
      args['resource'] = this.resourceId;
      args['frame'] = this.frameNumber;
      return JSON.stringify(args);
    },

    LoadUserLayers: function(activeLayerId) {
      var that = this;
      axios.post('../api/list-layers',
                 this.CreatePostPayload({}))
        .then(function(response) {
          that.userLayers = response.data['user-layers'];

          if (that.userLayers.length == 0) {
            that.CreateUserLayer();
          } else if (activeLayerId !== undefined) {
            that.activeUserLayerId = activeLayerId;
          } else {
            that.activeUserLayerId = that.userLayers[0].id;
          }
        })
        .catch(function() {
          console.error('Cannot load the saved annotations');
        });
    },

    CreateUserLayer: function() {
      var that = this;
      axios.post('../api/create-user-layer',
                 this.CreatePostPayload({}))
        .then(function(response) {
          that.LoadUserLayers(response.data.id);
        })
        .catch(function() {
          console.error('Cannot create a new layer');
        });
    },

    SaveUserLayer: function(layer) {
      var that = this;
      axios.post('../api/save-user-layer',
                 this.CreatePostPayload({
                   'layer': layer
                 }))
        .catch(function() {
          console.error('Cannot save layer');
        });
    },

    LoadUserFeatures: function() {
      console.assert(this.drawSource !== null);  // InitializeAnnotations() must have been invoked

      this.showSpinner = true;

      var that = this;
      axios.post('../api/load-user-features',
                 this.CreatePostPayload({}))
        .then(function(response) {
          that.drawSource.clear();

          // We check that the original layer is still available (could have been some write error)
          var availableLayerIds = [];
          for (let i = 0; i < that.userLayers.length; i++) {
            availableLayerIds.push(that.userLayers[i].id);
          }

          for (let i = 0; i < response.data.features.length; i++) {
            var layerId = response.data.features[i]['layer-id'];

            if (layerId !== undefined &&
                availableLayerIds.includes(layerId)) {
              var geometry = UnserializeFeature(response.data.features[i]);

              if (geometry !== null) {
                var feature = new ol.Feature(geometry);
                feature.set('layer-id', layerId);
                that.drawSource.addFeature(feature);
              }
            }
          }

          // Now that the features are loaded, we can install the save callback
          that.drawSource.on('addfeature', function (e) {
            that.SaveUserFeatures();
          });
          that.drawSource.on('removefeature', function (e) {
            that.SaveUserFeatures();
          });
        })
        .catch(function() {
          console.error('Cannot load user features');
        })
        .finally(function() {
          that.showSpinner = false;
        });
    },

    SaveUserFeatures: function()
    {
      var that = this;

      function Execute()
      {
        var features = [];

        that.drawSource.getFeatures().forEach(function (feature, index) {
          var item = SerializeFeature(feature);

          if (item !== null) {
            item['layer-id'] = feature.get('layer-id');
            features.push(item);
          }
        });

        that.isPendingChange = false;
        that.isSaving = true;
        that.showSpinner = true;
        window.addEventListener('beforeunload', BeforeUnloadHandler);

        axios.post('../api/save-user-features',
                   that.CreatePostPayload({
                     'features': features
                   }))
          .then(function() {
            // Success
          })
          .catch(function() {
            console.error('Cannot save the annotations');
          })
          .finally(function() {
            console.assert(that.isSaving === true);

            if (that.isPendingChange) {
              Execute();
            } else {
              that.isSaving = false;
              that.showSpinner = false;
              window.removeEventListener('beforeunload', BeforeUnloadHandler);
            }
          });
      }

      this.isPendingChange = true;

      if (!this.isSaving) {
        Execute();
      }
    },

    DeleteUserLayer: function(id) {
      this.pendingDelete = id;
      this.modalDeleteUserLayer.show();
    },

    UserLayerDeleteConfirmed: function() {
      var layerId = this.pendingDelete;   // The ID of the layer to be removed

      this.modalDeleteUserLayer.hide();
      var that = this;
      axios.post('../api/delete-user-layer',
                 this.CreatePostPayload({
                   'layer-id': layerId
                 })
                )
        .then(function(response) {
          that.LoadUserLayers();

          // Remove the features that were part of this layer
          that.drawSource.getFeatures().forEach(function(feature) {
            if (feature.get('layer-id') === layerId) {
              that.drawSource.removeFeature(feature);
            }
          });
        })
        .catch(function(error) {
          console.error('Cannot delete the layer');
        });
    },

    TakeScreenshot: function() {
      modernScreenshot.domToBlob(document.body, {
        filter: function (element) {
          if (element.classList === undefined) {
            return true;
          } else {
            return (element.id !== 'toolbar-top' &&
                    element.id !== 'toolbar-left' &&
                    !element.classList.contains('tooltip') &&
                    !element.classList.contains('ol-control'));  // "+", "-", and "rotate" buttons
          }
        }
      })
        .then(function (blob) {
          navigator.clipboard.write([
            new ClipboardItem({
              'image/png': blob
            })
          ])
            .then(function () {
              alert('Screenshot copied to clipboard!');
            })
            .catch(function (error) {
              alert('Could not copy screenshot\n\n(' + error + ')');
            });
        });
    },

    LoadAnnotationsInfo: function() {
      var that = this;
      axios.post('../api/annotations-info',
                 this.CreatePostPayload({}))
        .then(function(response) {
          that.projectName = response.data['project-name'];
          that.projectDescription = response.data['project-description'];
          that.projectUser = response.data['user'];
          that.alertNotPersistent = !response.data['persistent-annotations'];
        });
    },

    // -----------------------------------------------------------------------
    // Annotation selection panel
    // -----------------------------------------------------------------------

    AddReadOnlyProperty: function(label, value) {
      this.annotationProperties.push({
        type: 'readonly',
        label: label,
        value: value
      });
    },

    AddEditableProperty: function(label, value, featureProp) {
      this.annotationProperties.push({
        type: 'editable',
        label: label,
        value: value,
        featureProp: featureProp
      });
    },

    AddDropdownProperty: function(label, options, selectedValue, featureProp) {
      this.annotationProperties.push({
        type: 'dropdown',
        label: label,
        value: selectedValue,
        options: options,
        featureProp: featureProp
      });
    },

    UpdateAnnotationProperty: function(prop) {
      if (this.selectedFeature && prop.featureProp) {
        this.selectedFeature.set(prop.featureProp, prop.value);
      }
    },

    FocusAnnotation: function() {
      if (this.selectedFeature) {
        bootstrap.Offcanvas.getOrCreateInstance(document.getElementById('right-panel')).hide();
        this.map.getView().fit(this.selectedFeature.getGeometry().getExtent(), { padding: [40, 40, 40, 40], duration: 300 });
      }
    },

    // -----------------------------------------------------------------------
    // Rotation controls (driven from the popover)
    // -----------------------------------------------------------------------

    ResetRotation: function() {
      this.rotationDeg = 0;
      this.SetMapRotation();
    },

    RotateBy: function(deg) {
      this.rotationDeg = parseInt(this.rotationDeg) + deg;

      while (this.rotationDeg > 180) {
        this.rotationDeg -= 360;
      }

      while (this.rotationDeg < -180) {
        this.rotationDeg += 360;
      }

      this.SetMapRotation();
    },

    SetMapRotation: function() {
      this.map.getView().setRotation(this.rotationDeg / 180 * Math.PI);
    },

    // -----------------------------------------------------------------------
    // Draw tool activation
    // -----------------------------------------------------------------------

    DeactivateAll: function() {
      this.map.removeInteraction(this.drawLine);
      this.map.removeInteraction(this.drawPoint);
      this.map.removeInteraction(this.drawCircle);
      this.map.removeInteraction(this.drawRectangle);
      this.map.removeInteraction(this.drawClosedPolygon);
      this.map.removeInteraction(this.drawFreehand);
      this.map.removeInteraction(this.drawFreehandLine);
      this.map.removeInteraction(this.moveFeature);
      this.map.removeInteraction(this.modifyFeature);
      this.map.removeInteraction(this.selectAnnotation);

      this.activeDrawTool = null;
      this.map.getViewport().style.cursor = '';

      if (this.selectAnnotation !== null) {
        this.selectAnnotation.getFeatures().clear();
      }

      this.selectedFeature = null;
    },

    ToggleSelectTool: function() {
      var wasActive = this.activeDrawTool === 'select';
      this.DeactivateAll();
      if (!wasActive) {
        this.map.addInteraction(this.selectAnnotation);
        this.activeDrawTool = 'select';
        this.map.getViewport().style.cursor = 'pointer';
      }
    },

    ToggleDrawTool: function(toolName) {
      var interactions = {
        'line':           this.drawLine,
        'point':          this.drawPoint,
        'circle':         this.drawCircle,
        'rectangle':      this.drawRectangle,
        'closed-polygon': this.drawClosedPolygon,
        'freehand':       this.drawFreehand,
        'freehand-line':  this.drawFreehandLine,
        'move':           this.moveFeature,
        'modify':         this.modifyFeature
      };

      var cursors = {
        'freehand':      'crosshair',
        'freehand-line': 'crosshair'
      };

      // Draw tools that keep selectAnnotation active to highlight the newly drawn feature
      var drawTools = ['line', 'point', 'circle', 'rectangle', 'closed-polygon', 'freehand', 'freehand-line'];

      var wasActive = this.activeDrawTool === toolName;
      this.DeactivateAll();
      if (!wasActive) {
        this.map.addInteraction(interactions[toolName]);
        if (drawTools.indexOf(toolName) !== -1) {
          this.map.addInteraction(this.selectAnnotation);  // kept active to show blue highlight
        }
        this.activeDrawTool = toolName;
        var cursor = cursors[toolName];
        if (cursor) {
          this.map.getViewport().style.cursor = cursor;
        }
      }
    },

    DeleteSelectedAnnotation: function() {
      var selected = this.selectAnnotation.getFeatures();
      if (selected.getLength() > 0) {
        this.modalDeleteAnnotation.show();
      }
    },

    ConfirmDeleteAnnotation: function() {
      var selected = this.selectAnnotation.getFeatures();

      var that = this;
      selected.forEach(function(feature) {
        that.drawSource.removeFeature(feature);
      });

      selected.clear();

      this.selectedFeature = null;
      this.modalDeleteAnnotation.hide();
    },

    // -----------------------------------------------------------------------
    // Panel animation
    // -----------------------------------------------------------------------

    InitializePanelAnimation: function() {
      var that = this;
      var panel = document.getElementById('right-panel');
      var toggle = document.getElementById('right-panel-toggle');
      var isResizing = false;

      function ResizingLoop() {
        toggle.style.right = (window.innerWidth - panel.getBoundingClientRect().left) + 'px';
        if (isResizing) {
          requestAnimationFrame(ResizingLoop);
        }
      }

      function StartResizing() {
        isResizing = true;
        requestAnimationFrame(ResizingLoop);
      }

      function StopResizing() {
        isResizing = false;
        ResizingLoop();
      }

      ResizingLoop();

      panel.addEventListener('hide.bs.offcanvas', function() {
        that.panelOpen = false;
        StartResizing();
      });
      panel.addEventListener('hidden.bs.offcanvas', StopResizing);

      panel.addEventListener('show.bs.offcanvas', function() {
        that.panelOpen = true;
        StartResizing();
      });
      panel.addEventListener('shown.bs.offcanvas', StopResizing);
    },

    // -----------------------------------------------------------------------
    // Pyramid loading and map initialization
    // -----------------------------------------------------------------------

    LoadPyramid: function() {
      var that = this;

      if (this.level == 'series')
      {
        axios.get('../pyramids/' + this.resourceId)
          .then(function(response) {
            that.InitializePyramid(response.data, '../tiles/' + that.resourceId + '/');
          })
          .catch(function(error) {
            alert('Error - Cannot get the pyramid structure of series: ' + that.resourceId);
          });
      }
      else if (this.level == 'instance')
      {
        axios.get('../frames-pyramids/' + this.resourceId + '/' + this.frameNumber)
          .then(function(response) {
            that.InitializePyramid(response.data, '../frames-tiles/' + that.resourceId + '/' + that.frameNumber + '/');
          })
          .catch(function(error) {
            alert('Error - Cannot get the pyramid structure of frame ' + that.frameNumber + ' of instance: ' + that.resourceId);
          });
      }
    },

    InitializePyramid: function(pyramid, tilesBaseUrl) {
      this.mapBackground = pyramid['BackgroundColor'];  // New in WSI 2.1

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

      if (metersPerUnit) {
        this.showMagnificationButtons = true;
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
        content: document.getElementById('popover-rotate')
      });

      new bootstrap.Popover(document.getElementById('button-adjustments'), {
        placement: 'right',
        container: 'body',
        html: true,
        content: document.getElementById('popover-adjustments')
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

        /*new ol.control.ScaleLine({
          minWidth: 100
          })*/
        new MicroscopeScaleLine({
          minWidth: 100,
          referenceMagnification: this.referenceMagnification
        })

      ]);

      if (this.imageDescription !== null) {
        controls.extend([
          new ol.control.Attribution({
            attributions: this.imageDescription,
            collapsible: false
          })
        ]);
      }

      var tileLayer = new ol.layer.Tile({
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

      var that = this;
      tileLayer.on('prerender', (event) => {
        const context = event.context;

        if (context) {
          context.save();
          var brightness = Math.pow(4, that.brightness / 100.0);  // Ranges between 0.25 and 4
          var contrast = Math.pow(4, that.contrast / 100.0);      // Ranges between 0.25 and 4
          var saturation = Math.pow(4, that.saturation / 100.0);  // Ranges between 0.25 and 4
          context.filter =
            'brightness(' + brightness.toFixed(4) + ') ' +
            'contrast(' + contrast.toFixed(4) + ')' +
            'saturate(' + saturation.toFixed(4) + ')';
          // 'hue-rotate(' + that.hue + 'deg)';
        }
      });

      tileLayer.on('postrender', (event) => {
        const context = event.context;

        if (context) {
          context.restore();
        }
      });

      this.map = new ol.Map({
        target: 'map',
        layers: [ tileLayer ],
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
      [ 'toolbar-left', 'toolbar-top' ].forEach(function(id) {
        var el = document.getElementById(id);
        [ 'pointerdown', 'pointerup', 'pointermove', 'click' ].forEach(function(type) {
          el.addEventListener(type, function(e) {
            e.stopPropagation();
          });
        });
      });

      // Re-append toolbars inside the map viewport so they inherit OL's scaling
      var viewport = this.map.getViewport();
      viewport.appendChild(document.getElementById('toolbar-left'));
      viewport.appendChild(document.getElementById('toolbar-top'));

      this.map.once('postrender', function() {
        // Match Bootstrap button size to OL button size
        var olBtnSize = document.querySelector('.ol-zoom button').offsetWidth + 'px';
        document.querySelectorAll('.icon-btn').forEach(function(el) {
          el.style.width = olBtnSize;
          el.style.height = olBtnSize;
        });

        // Move the top toolbar directly right to the zoom control, regardless of scaling
        var zoomEl = document.querySelector('.ol-zoom');
        document.getElementById('toolbar-top').style.left = (zoomEl.offsetLeft + zoomEl.offsetWidth) + 'px';
        document.getElementById('toolbar-top').style.top = zoomEl.offsetTop + 'px';

        // Move the left toolbar directly below the zoom control, regardless of scaling
        document.getElementById('toolbar-left').style.left = zoomEl.offsetLeft + 'px';
        document.getElementById('toolbar-left').style.top = (zoomEl.offsetTop + zoomEl.offsetHeight) + 'px';

        // Move the vertical buttons below the rotate control, regardless of scaling
        var rotateEl = document.querySelector('.ol-rotate');
        document.getElementById('toolbar-left-content').style.top = (rotateEl.offsetTop + rotateEl.offsetHeight) + 'px';
      });

      this.map.getView().fit(extent, this.map.getSize());

      this.toolbarsVisible = true;
      this.InitializeAnnotations();
    },

    ResetAdjustments: function() {
      this.brightness = 0;
      this.contrast = 0;
      this.saturation = 0;
      this.map.render();
    },

    // -----------------------------------------------------------------------
    // Drawing annotations
    // -----------------------------------------------------------------------

    InitializeAnnotations: function() {
      function GetLayerById(id) {
        for (var i = 0; i < app.userLayers.length; i++) {
          if (app.userLayers[i].id == id) {
            return app.userLayers[i];
          }
        }
        return null;
      }

      function GetLayerOfFeature(feature) {
        var layerId = feature.get('layer-id');
        console.assert(layerId !== null);
        var layer = GetLayerById(layerId);
        console.assert(layer !== null);
        return layer;
      }

      // Single vector source holding all features from all layers
      this.drawSource = new ol.source.Vector();

      this.drawLayer = new ol.layer.Vector({
        source: this.drawSource,
        style: function(feature) {
          var layer = GetLayerOfFeature(feature);
          if (layer.visible) {
            return CreateLayerStyle(layer.color);
          } else {
            return null;
          }
        }
      });

      this.map.addLayer(this.drawLayer);

      // Shared annotations: a separate read-only source for all imported shared layers
      this.sharedSource = new ol.source.Vector();
      this.sharedLayer = new ol.layer.Vector({
        source: this.sharedSource,
        style: function(feature) {
          var entry = GetSharedLayerById(feature.get('shared-layer-id'));
          if (!entry || !entry.visible) {
            return null;
          }
          return CreateLayerStyle(entry.color);
        }
      });
      this.map.addLayer(this.sharedLayer);

      // Draw interactions (inactive until toggled)
      this.drawLine = new ol.interaction.Draw({ source: this.drawSource, type: 'LineString' });
      this.drawPoint = new ol.interaction.Draw({ source: this.drawSource, type: 'Point' });
      this.drawCircle = new ol.interaction.Draw({ source: this.drawSource, type: 'Circle' });
      this.drawRectangle = new ol.interaction.Draw({
        source: this.drawSource,
        type: 'Circle',
        geometryFunction: ol.interaction.Draw.createBox()
      });
      this.drawClosedPolygon = new ol.interaction.Draw({ source: this.drawSource, type: 'Polygon' });
      this.drawFreehand = new ol.interaction.Draw({ source: this.drawSource, type: 'Polygon', freehand: true });
      this.drawFreehandLine = new ol.interaction.Draw({ source: this.drawSource, type: 'LineString', freehand: true });

      this.moveFeature = new ol.interaction.Translate({ source: this.drawSource });
      this.modifyFeature = new ol.interaction.Modify({ source: this.drawSource });

      // Select interaction (inactive until toggled).
      // The condition restricts user-click selection to the dedicated select tool only,
      // preventing spurious selection events when starting a draw near an existing feature.
      this.selectAnnotation = new ol.interaction.Select({
        layers: [ this.drawLayer ],
        filter: function(feature) {
          return GetLayerOfFeature(feature).visible;
        },
        condition: function(e) {
          return ol.events.condition.singleClick(e) && app.activeDrawTool === 'select';
        },
        hitTolerance: 5,  /* pixels around the feature that count as a hit */
        style: new ol.style.Style({
          stroke: new ol.style.Stroke({ color: 'blue', width: 3 }),
          image: new ol.style.Circle({
            radius: 5,
            fill: new ol.style.Fill({ color: 'blue' })
          })
        })
      });


      var that = this;

      function preventDoubleClickZoom() {
        that.map.getInteractions().forEach(function(interaction) {
          if (interaction instanceof ol.interaction.DoubleClickZoom) {
            interaction.setActive(false);
            setTimeout(function() { interaction.setActive(true); }, 50);
          }
        });
      }

      function onDrawEnd(e, callPreventDoubleClickZoom) {
        e.feature.set('layer-id', app.activeUserLayerId);
        if (callPreventDoubleClickZoom) {
          preventDoubleClickZoom();
        }
        that.selectAnnotation.getFeatures().clear();
        that.selectAnnotation.getFeatures().push(e.feature);
        that.selectAnnotation.dispatchEvent({ type: 'select', selected: [e.feature], deselected: [] });
      }

      this.drawLine.on('drawend', function(e) { onDrawEnd(e, true); });
      this.drawPoint.on('drawend', function(e) { onDrawEnd(e, true); });
      this.drawCircle.on('drawend', function(e) { onDrawEnd(e, true); });
      this.drawRectangle.on('drawend', function(e) { onDrawEnd(e, true); });
      this.drawClosedPolygon.on('drawend', function(e) { onDrawEnd(e, true); });
      this.drawFreehand.on('drawend', function(e) { onDrawEnd(e, false); });
      this.drawFreehandLine.on('drawend', function(e) { onDrawEnd(e, false); });

      this.moveFeature.on('translateend', function(e) { that.SaveUserFeatures(); });
      this.modifyFeature.on('modifyend', function(e) { that.SaveUserFeatures(); });

      this.selectAnnotation.on('select', function(e) {
        that.annotationProperties = [];

        if (e.selected.length === 1) {
          var feature = e.selected[0];
          that.selectedFeature = feature;

          var geometry = feature.getGeometry();

          if (geometry.getType() === 'LineString') {
            that.AddReadOnlyProperty('Length', FormatLength(geometry, that.map.getView().getProjection()));
          } else {
            // TODO
            that.AddReadOnlyProperty('Length', '1.23 mm');
            that.AddReadOnlyProperty('Bounding box', '100 x 200 px');
            that.AddReadOnlyProperty('Surface', '0.05 mm²');
            that.AddEditableProperty('Label', feature.get('label') || '', 'label');
            that.AddDropdownProperty('Category', [
              { value: 'tumor',    label: 'Tumor' },
              { value: 'stroma',   label: 'Stroma' },
              { value: 'necrosis', label: 'Necrosis' }
            ], feature.get('category') || '', 'category');
          }
          bootstrap.Offcanvas.getOrCreateInstance(document.getElementById('right-panel')).show();
        } else {
          that.selectedFeature = null;
        }
      });

      this.LoadUserFeatures();
    },

    // -----------------------------------------------------------------------
    // TODO - Shared layers
    // -----------------------------------------------------------------------

    ShowImportSharedLayerModal: function() {
      this.importSelectedUser = '';
      this.importSelectedLayer = '';
      this.importAvailableLayers = [];
      this.importAvailableSharedLayers = {};
      this.importUsersLoading = true;
      this.importUsersFailed = false;
      this.modalImportSharedLayer.show();

      var that = this;
      axios.post('../api/shared-layers',
                 this.CreatePostPayload({}))
        .then(function(response) {
          that.importAvailableSharedLayers = response.data;
          that.importUsersLoading = false;
        })
        .catch(function(error) {
          that.importUsersLoading = false;
          that.importUsersFailed = true;
        });
    },

    ImportUserChanged: function() {
      this.importSelectedLayer = '';
      if (this.importSelectedUser && this.importAvailableSharedLayers[this.importSelectedUser]) {
        this.importAvailableLayers = this.importAvailableSharedLayers[this.importSelectedUser].layers || [];
      } else {
        this.importAvailableLayers = [];
      }
    },

    ImportSharedLayerConfirmed: function() {
      var userId = this.importSelectedUser;
      var layerId = this.importSelectedLayer;
      if (!userId || !layerId) { return; }

      var layerName = '';
      for (var i = 0; i < this.importAvailableLayers.length; i++) {
        if (this.importAvailableLayers[i].id === layerId) {
          layerName = this.importAvailableLayers[i].name;
          break;
        }
      }

      var sharedLayers = this.sharedLayers;
      var alreadyImported = sharedLayers.some(function(s) {
        return s.userId === userId && s.id === layerId;
      });
      if (!alreadyImported) {
        /*var entry = {
          id: layerId,
          userId: userId,
          author: userId,
          name: layerName,
          color: predefinedPalette[sharedLayers.length % predefinedPalette.length],
          visible: true
        };
        sharedLayers.push(entry);
        LoadSharedLayerAnnotations(entry);*/

        // TODO
      }

      this.modalImportSharedLayer.hide();
    },

    ReloadSharedLayers: function() {
      // TODO
    }
  }
});


function IsNear(a, b)
{
  return Math.abs(a - b) <= 0.01;
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


function CreateLayerStyle(color)
{
  function HexToRGBA(hex, alpha)
  {
    var r = parseInt(hex.slice(1, 3), 16);
    var g = parseInt(hex.slice(3, 5), 16);
    var b = parseInt(hex.slice(5, 7), 16);
    return 'rgba(' + r + ',' + g + ',' + b + ',' + alpha + ')';
  }

  return new ol.style.Style({
    stroke: new ol.style.Stroke({ color: color, width: 2 }),
    fill: new ol.style.Fill({ color: HexToRGBA(color, 0.2) }),
    // The "image" style is used by point annotations
    image: new ol.style.Circle({
      radius: 5,
      fill: new ol.style.Fill({ color: color })
    })
  });
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


function BeforeUnloadHandler(event)
{
  event.preventDefault();

  // Included for legacy support, e.g. Chrome/Edge < 119
  event.returnValue = true;
};




function SetMagnification(map, referenceMagnification, magnification)
{
  var view = map.getView();
  var projection = view.getProjection();

  if (projection.getMetersPerUnit()) {  // Ensure that "metersPerUnit" is not null
    var resolution = referenceMagnification / magnification;

    view.animate({
      resolution: view.getConstrainedResolution(resolution),
      duration: 250
    });
  }
}


function GetMagnification(map, referenceMagnification)
{
  var view = map.getView();
  var projection = view.getProjection();

  if (projection.getMetersPerUnit() !== undefined) {  // Ensure that "metersPerUnit" is not null
    var resolution = view.getResolution();

    return referenceMagnification / resolution;
  }
}


/**
 * A ScaleLine control that also displays the equivalent microscope
 * objective magnification (4x, 10x, 40x,...) for the current zoom level.
 */
class MicroscopeScaleLine extends ol.control.ScaleLine {
  constructor(options = {}) {
    super(options);

    this.referenceMagnification_ = options.referenceMagnification;
    console.assert(this.referenceMagnification_ !== undefined);

    this.magnificationElement_ = document.createElement('div');
    this.magnificationElement_.className = 'ol-scale-magnification';
    this.element.appendChild(this.magnificationElement_);
  }

  updateElement_() {
    super.updateElement_();

    var map = this.getMap();
    if (map && this.magnificationElement_) {
      var magnification = GetMagnification(map, this.referenceMagnification_);
      if (magnification) {
        this.magnificationElement_.innerText = magnification.toFixed(2) + 'x';
      }
    }
  }
}
