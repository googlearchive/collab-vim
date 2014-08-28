Collaborative Vim
=================

This is an experiment that combines the Portable Native Client (PNaCl) with the
JavaScript Drive Realtime API to enable collaborative editing in Vim. This app
runs Vim *natively* in the browser and displays the UI with hterm, a JS terminal
emulator.

The PNaCl and JS code communicate to keep the Realtime model of the document in
sync with Vim's own file buffer. Realtime events from JS are passed to Vim and
applied to the file. Local edits within Vim are passed to JS to edit the
Realtime model.

This is not a Vim plugin. Arguably, a Vim plugin would not be sufficient for
robust collaboration. Vim is able to handle asynchronous edit events due to
modifications to its source.

Building
--------
From `vim73/`, run `./make_nacl.sh`. After a successful build, run
`./make_nacl.sh install`. There will be errors, see TODOs below. Finally, copy
`vim73/src/publish/vim_pnacl.final.pexe` to `web/vim_pnacl.pexe`.

Running
-------
To run on your own domain or machine, first set up the Drive API for your
project by following step 1 of [Creating a Realtime Application](https://developers.google.com/drive/realtime/application).
You will need to reassign the project ID's in `web/vim_realtime.js`.

The URL you launch the app from must be the same as the JavaScript Origins URL
you specified when setting up the project OAuth token.

You can run a local web server with `./httpd.py`, but note you must use a DNS
name to access your local machine. The Drive API doesn't recognize `localhost`
as a valid origin.

TODOs
-----
Although this project is functional, there is a lot left to do! Check [Issues](https://github.com/GoogleChrome/collab-vim/issues) if you want to contribute or to file a bug.
