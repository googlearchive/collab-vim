/**
 * TODO(zpotter): Add legal boilerplate
 */

// Load the Picker API for Drive file selection.
gapi.load('picker'); 

// Set app specific NaClTerm variables.
NaClTerm.prefix = 'vim'
NaClTerm.nmf = 'vim.nmf'

/**
 * A class used for tracking the indices of recently used lines. When text is
 * inserted or deleted from a line, the index of the line in the document is
 * unknown. To avoid frequent O(n) searches for line numbers, this cache can be
 * used to track shifting of a set of lines.
 * @param {gapi.drive.realtime.CollaborativeList} src The list containing the 
 *    lines.
 * @param {number} opt_capacity The maximum size of the LRU cache. If 0 or not
 *    provided, uses a default cache size.
 */
function IndexCache(src, opt_capacity) {
    // The source collection, used for initializing cache elements:
    this.source = src;
    // A map of line ID's to indices in 'source':
    this.indices = {};
    var capacity = opt_capacity || 50;
    // A circular queue of line ID's:
    this.lru = new Array(capacity);
    // The index of the oldest line in 'lru':
    this.oldest = 0;
}

/**
 * Get the index of a line in the source list. If the line is not in the cache,
 * its index is found in O(n) time and then added to the cache.
 * @param {gapi.drive.realtime.CollaborativeString} line The line in 'source'.
 * @return {number} The index of 'line' in 'source', or 'undefined' if not in
 *    the list.
 */
IndexCache.prototype.indexOf = function(line) {
  // If line is not cached, find its index the hard way.
  if (this.indices[line.id] == null) {
    // Find index of line, O(n).
    var index = this.source.indexOf(line);
    // Add to cache if line is in source.
    if (index >= 0)
      this.add(line, index);
  }
  // Get cached index.
  return this.indices[line.id];
}

/**
 * Add a line to the cache, given a known index.
 * @param {gapi.drive.realtime.CollaborativeString} line The line to cache.
 * @param {number} index The current index of 'line' in 'source'.
 */
IndexCache.prototype.add = function(line, index) {
  // Remove oldest element from cache.
  var oldestElem = this.lru[this.oldest];
  delete this.indices[oldestElem];
  // Add new element to cache.
  this.lru[this.oldest] = line.id;
  this.indices[line.id] = index;
  // Advance oldest pointer.
  this.oldest++;
  if (this.oldest >= this.lru.length)
    this.oldest = 0;
}

/**
 * When elements are added or removed from the source list, this function must
 * be called to update the indices of the cached strings.
 * @param {number} index The index of the first modified element in the source
 *    list.
 * @param {number} amount The number of elements inserted or removed from the
 *    source list.
 */
IndexCache.prototype.shiftFrom = function(index, amount) {
  // Add 'amount' to each element with cached index >= 'index'.
  for (var line in this.indices) {
    if (this.indices[line] >= index) {
      this.indices[line] += amount;
    }
  }
}

/**
 * @namespace Realtime <--> Vim logic namespace.
 */
var rtvim =  rtvim || {};

/**
 * String keys and values for parsing and creating collaborative edit messages.
 * @type {string}
 */
var TYPE_APPEND_LINE = 'append_line';
var TYPE_INSERT_TEXT = 'insert_text';
var TYPE_REMOVE_LINE = 'remove_line';
var TYPE_DELETE_TEXT = 'delete_text';
var TYPE_REPLACE_LINE = 'replace_line';
var TYPE_KEY = 'collabedit_type';
var LINE_KEY = 'line';
var TEXT_KEY = 'text';
var INDEX_KEY = 'index';
var LENGTH_KEY = 'length';

/**
 * The index cache for tracking recently used lines.
 * @type {IndexCache}
 */
rtvim.lcache = null;

/**
 * The Realtime client.
 * @type {rtclient.RealtimeLoader}
 */
rtvim.realtimeLoader = null;

/**
 * The Realtime document.
 * @type {gapi.drive.realtime.Document}
 */
rtvim.doc = null;

/**
 * A flag indicating if Vim needs a file sync message.
 * @type {boolean}
 */
rtvim.needSync = false;

/**
 * Prompt the user for a new filename. Creates and then opens the new file.
 */
rtvim.createInDrive = function() {
  var filename = prompt('Enter a new filename', rtvim.rtOptions.defaultTitle);
  if (!filename) return;
  rtclient.createRealtimeFile(filename, rtvim.rtOptions.newFileMimeType,
      rtvim.openFromDrive);
}

/**
 * Opens a Drive Realtime file.
 * @param {object} opt_file A file to open in Vim. If not provided, a Drive
 *    dialog will be presented to the user to select a file.
 */
rtvim.openFromDrive = function(opt_file) {
  if (!file) {
    var token = gapi.auth.getToken().access_token;
    var view = new google.picker.View(google.picker.ViewId.DOCS);
    view.setMimeTypes(rtvim.rtOptions.newFileMimeType);
    var picker = new google.picker.PickerBuilder()
        .enableFeature(google.picker.Feature.NAV_HIDDEN)
        .setAppId(rtvim.rtOptions.appId)
        .setOAuthToken(token)
        .addView(view)
        //.addView(new google.picker.DocsUploadView())
        .setCallback(function(data) {
            console.log(data);
            if (data.action == google.picker.Action.PICKED)
              rtvim.openFromDrive(data.docs[0]);
          })
        .build();
    picker.setVisible(true);
    return;
  }
  // Disable buttons for opening files 
  document.getElementById('createButton').disabled = true;
  document.getElementById('openButton').disabled = true;
  // Enable sharing button
  document.getElementById('shareButton').disabled = false;

  rtvim.realtimeLoader.redirectTo([file.id],
      rtvim.realtimeLoader.authorizer.userId);
}

/**
 * Presents the Drive sharing dialog to the user.
 */
rtvim.shareDocument = function () {
  var shareClient = new gapi.drive.share.ShareClient(rtvim.rtOptions.appId);
  shareClient.setItemIds(rtclient.params['fileIds'].split(','));
  shareClient.showSettingsDialog();
}

/**
 * Handles incoming messages from NaCl. The 'this' variable refers to the 
 * Realtime document.
 * @param {object} e A NaCl message.
 * @return {boolean} True if the message was consumed, otherwise false.
 */
rtvim.applyLocalEdit = function(e) {
  // Check for vim ready message
  if (e.data == '_vimready') {
    // Only sync if the Realtime Document has been loaded. If Realtime
    // isn't yet ready, it will sync once the file loads.
    rtvim.needSync = true;
    if (rtvim.doc) rtvim.syncModel(rtvim.doc);
    return true;
  }
  // Skip anything that doesn't look like a collabedit message.
  if (!e.data || !e.data['collabedit_type']) return false;
  // console.log('HANDLE MESSAGE', e.data);
  // Modify the realtime model on behalf of the vim user.
  // Remember, collabedit line numbers start at 1, NOT 0!
  var collabedit = e.data;
  var rtLines = this.getModel().getRoot().get('vimlines');
  if (collabedit[TYPE_KEY] == TYPE_REPLACE_LINE) {
    rtLines.get(collabedit[LINE_KEY] - 1).setText(collabedit[TEXT_KEY]);

  } else if (collabedit[TYPE_KEY] == TYPE_APPEND_LINE) {
    // Create new collaborative string and assign event listeners.
    var lineString = this.getModel().createString(collabedit[TEXT_KEY]);
    lineString.addEventListener(gapi.drive.realtime.EventType.TEXT_INSERTED, 
      rtvim.onTextInserted.bind(lineString));
    lineString.addEventListener(gapi.drive.realtime.EventType.TEXT_DELETED,
      rtvim.onTextDeleted.bind(lineString));
    rtLines.insert(collabedit[LINE_KEY], lineString);

  } else if (collabedit[TYPE_KEY] == TYPE_REMOVE_LINE) {
    rtLines.remove(collabedit[LINE_KEY] - 1);

  } else if (collabedit[TYPE_KEY] == TYPE_INSERT_TEXT) {
    rtLines.get(collabedit[LINE_KEY] - 1)
       .insertString(collabedit[INDEX_KEY], collabedit[TEXT_KEY]);

  } else if (collabedit[TYPE_KEY] == TYPE_DELETE_TEXT) {
    var start = collabedit[INDEX_KEY];
    var end = start + collabedit[LENGTH_KEY];
    rtLines.get(collabedit[LINE_KEY] - 1).removeRange(start, end);

  } else {
    console.log('Unrecognized collabedit type from Vim: ' +
        collabedit[TYPE_KEY]);
  }
  return true;
}

/**
 * Called once the app has been authorized with Drive.
 */
rtvim.driveAuthorized = function() {
  // Enable buttons for opening files 
  document.getElementById('createButton').disabled = false;
  document.getElementById('openButton').disabled = false;
}

/**
 * Called the first time that a Realtime model is created for a file. Used to
 * initialize the default model.
 * @param model {gapi.drive.realtime.Model} the Realtime root model object.
 */
rtvim.initializeModel = function(model) {
  var lines = model.createList();
  lines.push(model.createString('Hello Realtime World!'));
  lines.push(model.createString('Welcome to Vim!'));
  model.getRoot().set('vimlines', lines);
}

/**
 * Called when the Realtime file has been loaded. Initializes event handlers.
 * @param doc {gapi.drive.realtime.Document} the Realtime document.
 */
rtvim.onFileLoaded = function(doc) {
  console.log('Realtime File Loaded:');
  rtvim.doc = doc;
  // Enable the share button
  document.getElementById('shareButton').disabled = false;
  // Set up model event listeners
  var lines = doc.getModel().getRoot().get('vimlines');
  lines.addEventListener(gapi.drive.realtime.EventType.VALUES_ADDED, rtvim.onLineAdded);
  lines.addEventListener(gapi.drive.realtime.EventType.VALUES_REMOVED, rtvim.onLineRemoved);
  // Make sure there is at least one line in the doc
  if (lines.length == 0) {
    doc.getModel().getRoot().get('vimlines').push(doc.getModel().createString(''));  
  }
  for (var i = 0; i < lines.length; i++) {
    var line = lines.get(i);
    console.log((i+1)+': '+line.toString());
    line.addEventListener(gapi.drive.realtime.EventType.TEXT_INSERTED,
      rtvim.onTextInserted.bind(line));
    line.addEventListener(gapi.drive.realtime.EventType.TEXT_DELETED,
      rtvim.onTextDeleted.bind(line));
  }
  // Set up LRU cache for line numbers
  rtvim.lcache = new IndexCache(lines);
  if (rtvim.needSync) rtvim.syncModel(doc);
}

/**
 * Posts a message to the NaCl module.
 * @param {object} msg The message to send to native code.
 */
rtvim.postMessage = function(msg) {
  // console.log('POST MESSAGE', msg);
  foreground_process.postMessage(msg);
}

/**
 * Sends messages to Vim to sync the Realtime model with the file buffer.
 * @param {gapi.drive.realtime.Document} rtdoc The Realtime Document to sync.
 */
rtvim.syncModel = function(rtdoc) {
  // Fake a bunch of line append events
  var lines = rtdoc.getModel().getRoot().get('vimlines');
  for (var i = lines.length - 1; i >= 0; i--) {
    var appendedit = {};
    appendedit[TYPE_KEY] = TYPE_APPEND_LINE;
    appendedit[LINE_KEY] = 0;
    appendedit[TEXT_KEY] = lines.get(i).toString();
    // TODO remove for propper sync
    // First change is an insert because empty vim file starts with 1 empty line
    if (i == lines.length - 1) {
      appendedit[TYPE_KEY] = TYPE_INSERT_TEXT;
      appendedit[LINE_KEY] = 1;
      appendedit[INDEX_KEY] = 0;
    }
    rtvim.postMessage(appendedit);
  }
  rtvim.needSync = false;
}

/** 
 * Sends an append line event to Vim.
 * @param {gapi.drive.realtime.ValuesAddedEvent} ev The Realtime to send.
 */
rtvim.onLineAdded = function(ev) {
  // Ignore local events caused by our own edits
  if (ev.isLocal) return;
  // This event may contain more than 1 added line
  var lines = ev.values;
  var lnum = ev.index;
  // Construct collabedit messages to pass to Vim
  for (var i = 0; i < lines.length; i++) {
    // Add event listeners to the new line's CollaborativeString
    lines[i].addEventListener(gapi.drive.realtime.EventType.TEXT_INSERTED, 
      rtvim.onTextInserted.bind(lines[i]));
    lines[i].addEventListener(gapi.drive.realtime.EventType.TEXT_DELETED,
      rtvim.onTextDeleted.bind(lines[i]));

    var collabedit = {};
    collabedit[TYPE_KEY] = TYPE_APPEND_LINE;
    collabedit[LINE_KEY] = lnum + i;
    collabedit[TEXT_KEY] = lines[i].toString();
    // Let Vim know about the update
    rtvim.postMessage(collabedit);
  }
  // Update the indices of cached lines.
  rtvim.lcache.shiftFrom(lnum, lines.length);
}

/** 
 * Sends a remove line event to Vim.
 * @param {gapi.drive.realtime.ValuesRemovedEvent} ev The Realtime to send.
 */
rtvim.onLineRemoved = function(ev) {
  // Ignore local events caused by our own edits
  if (ev.isLocal) return;
  // This event may contain more than 1 removed line
  var lnum = ev.index;
  // Construct collabedit messages to pass to Vim
  for (var i = 0; i < ev.values.length; i++) {
    var collabedit = {};
    collabedit[TYPE_KEY] = TYPE_REMOVE_LINE;
    collabedit[LINE_KEY] = lnum + 1;
    // Let Vim know about the update
    rtvim.postMessage(collabedit);
  }
  // Update the indices of cached lines.
  rtvim.lcache.shiftFrom(lnum, -ev.values.length);
}

/** 
 * Sends an insert text event to Vim. The 'this' var refers to the
 * CollaborativeString that was modified.
 * @param {gapi.drive.realtime.TextInsertedEvent} ev The Realtime to send.
 */
rtvim.onTextInserted = function(ev) {
  // Ignore local events caused by our own edits
  if (ev.isLocal) return;
  var lnum = rtvim.lcache.indexOf(this);
  // Construct collabedit messages to pass to Vim
  var collabedit = {};
  collabedit[TYPE_KEY] = TYPE_INSERT_TEXT;
  collabedit[LINE_KEY] = lnum + 1;
  collabedit[INDEX_KEY] = ev.index;
  collabedit[TEXT_KEY] = ev.text;
  // Let Vim know about the update
  rtvim.postMessage(collabedit);
}

/** 
 * Sends a delete text event to Vim. The 'this' var refers to the
 * CollaborativeString that was modified.
 * @param {gapi.drive.realtime.TextInsertedEvent} ev The Realtime to send.
 */
rtvim.onTextDeleted = function(ev) {
  // Ignore local events caused by our own edits
  if (ev.isLocal) return;
  var lnum = rtvim.lcache.indexOf(this);
  // Construct collabedit messages to pass to Vim
  var collabedit = {};
  collabedit[TYPE_KEY] = TYPE_DELETE_TEXT;
  collabedit[LINE_KEY] = lnum + 1;
  collabedit[INDEX_KEY] = ev.index;
  collabedit[LENGTH_KEY] = ev.text.length;
  // Let Vim know about the update
  rtvim.postMessage(collabedit);
}

/**
 * Options for the Realtime loader.
 */
rtvim.rtOptions = {
  /**
   * The app ID from Drive developer console.
   */
  appId: '341213357015',

  /**
   * Client ID from the console.
   */
  clientId: '341213357015-061f6fqhmh061l88dl6qavoh99a98mn0.apps.googleusercontent.com',

  /**
   * The ID of the button to click to authorize. Must be a DOM element ID.
   */
  authButtonElementId: 'authorizeButton',

  /**
   * Function to be called when a Realtime model is first created.
   */
  initializeModel: rtvim.initializeModel,

  /**
   * Autocreate files right after auth automatically.
   */
  autoCreate: false,

  /**
   * The name of newly created Drive files.
   */
  defaultTitle: 'New Collaborative Vim File',

  /**
   * The MIME type of newly created Drive Files. By default the application
   * specific MIME type will be used:
   *     application/vnd.google-apps.drive-sdk.
   */
  newFileMimeType: null,

  /**
   * Function to be called every time a Realtime file is loaded.
   */
  onFileLoaded: rtvim.onFileLoaded,

  /**
   * Function to be called after authorization and before loading files.
   */
  afterAuth: rtvim.driveAuthorized
}

/**
 * Start the Realtime loader with the options.
 */
rtvim.startRealtime = function() {
  rtvim.realtimeLoader = new rtclient.RealtimeLoader(rtvim.rtOptions);
  rtvim.realtimeLoader.start();
}

// NaClTerm sets its own onload. Call it later as part of initialization.
var oldOnload = window.onload;
window.onload = function() {
  // Set up a NaCl message handler that intercepts messages before they reach
  // NaClTerm (which can't handle unexpected messages).
  var termHandleMessage = NaClTerm.prototype.handleMessage_;
  NaClTerm.prototype.handleMessage_= function(e) {
    // Attempt processing the message as a collaborative edit.
    var processed = rtvim.applyLocalEdit.apply(rtvim.doc, arguments);
    // If message wasn't recognised by this code, send to NaClTerm.
    if (!processed) termHandleMessage.apply(this, arguments);
  };

  // Call NaClTerm's onload function.
  if (oldOnload) oldOnload();
  // Start realtime.
  rtvim.startRealtime();

  // Set up other UI handlers.
  document.getElementById('createButton')
      .addEventListener('click', rtvim.createInDrive);
  document.getElementById('openButton')
      .addEventListener('click', rtvim.openFromDrive);
  document.getElementById('shareButton')
      .addEventListener('click', rtvim.shareDocument);
}
