NaClTerm.prefix = 'vim'
NaClTerm.nmf = 'vim.nmf'

function log(message) {
  console.log(message)
}

function fsErrorHandler(error) {
  log("Filesystem error: "+ error);
}

function runWithFile(file) {
  log("runWithFile: " + file.name)
  tempFS.root.getFile(file.name, {create: true},
    function(fileEntry) {
      window.tmpFileEntry = fileEntry
      fileEntry.createWriter(function(fileWriter) {
        // Note: write() can take a File or Blob object.
        fileWriter.write(file);
        log("File written to temporary filesystem\n");
        NaClTerm.argv = ['/tmp/' + file.name];
        NaClTerm.init();
      }, fsErrorHandler);
    }, fsErrorHandler);
}

function uploadDidChange(event) {
  var file = event.target.files[0];
  runWithFile(file);
}

function onInitFS(fs) {
  log("onInitFS");
  window.tempFS = fs
  if (window.fileEntryToLoad !== undefined) {
    window.fileEntryToLoad.file(function(file) {
        runWithFile(file);
    })
  } else {
    var upload = document.getElementById('infile');
    if (upload !== null) {
      upload.addEventListener('change', uploadDidChange, false);
    } else {
      NaClTerm.init();
    }
  }
}

function onInit() {
  navigator.webkitPersistentStorage.requestQuota(1024 * 1024,
    function(bytes) {
      window.webkitRequestFileSystem(window.TEMPORARAY, bytes, onInitFS)
    },
    function() {
      log("Failed to allocate space!\n");
      // Start the terminal even if FS failed to init.
      NaClTerm.init();
    }
  );
}

window.onload = function() {
  lib.init(function() {
    onInit();
  });
};
