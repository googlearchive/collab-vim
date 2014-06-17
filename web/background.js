/*
 * Copyright (c) 2013 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

'use strict';

function fsErrorHandler(error) {
  // TODO(sbc): Add popup that tells the user there was an error.
  console.log("Filesystem error: "+ error);
}

/**
 * Copyies one FileEntry object to another.  This is used to copy from
 * the TEMPORARAY filesystem in which the lauched files is edited back
 * to the entry that was passed at launch time.
 */
function copyFile(srcFile, destFile) {
  console.log("copyFile: " + srcFile.name + " -> " + destFile.name);

  srcFile.file(function(file) {
    destFile.createWriter(function(writer) {
      writer.onerror = fsErrorHandler;
      var doneTruncate = false;
      writer.onwriteend = function(e) {
        if (!doneTruncate) {
          console.log("done truncate");
          doneTruncate = true;
          writer.seek(0);
          writer.write(file);
        } else {
          console.log("done write");
          console.log(e);
        }
      };
      writer.truncate(file.size);
    }, fsErrorHandler);
  }, fsErrorHandler);
}

function saveFile(saveFileEntry) {
  window.webkitRequestFileSystem(window.TEMPORARAY, 0, function(tmpFS) {
    tmpFS.root.getFile(saveFileEntry.name, {create: false},
      function(tmpFileEntry) {
        copyFile(tmpFileEntry, saveFileEntry);
      }
    );
  });
}

function onLaunchedListener(launchData) {

  function onWindowCreated(win) {
    if (launchData.items) {
      var fileEntry = launchData.items[0].entry;
      console.log("Lauching vim with item: " + fileEntry)
      win.contentWindow.fileEntryToLoad = fileEntry;
    } else {
      console.log("Lauching vim")
    }

    function onWindowClosed() {
      console.log("onWindowClosed");
      var fileEntry = win.contentWindow.fileEntryToLoad;
      if (fileEntry) {
        saveFile(fileEntry);
      }
    }

    win.onClosed.addListener(onWindowClosed)
  }

  var props = {
    width: 600,
    height: 600
  };

  chrome.app.window.create('vim_app.html', props, onWindowCreated);
}

chrome.app.runtime.onLaunched.addListener(onLaunchedListener);
