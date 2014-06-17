/*
 * Copyright (c) 2013 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

'use strict';

lib.rtdep('lib.f',
          'hterm');

// CSP means that we can't kick off the initialization from the html file,
// so we do it like this instead.
window.onload = function() {
  lib.init(function() {
    NaClTerm.init();
  });
};

/**
 * The hterm-powered terminal command.
 *
 * This class defines a command that can be run in an hterm.Terminal instance.
 *
 * @param {Object} argv The argument object passed in from the Terminal.
 */
function NaClTerm(argv) {
  this.io = argv.io.push();
  this.argv_ = argv;
};

/**
 * The "no hang" flag for waitpid().
 * @type {number}
 */
NaClTerm.WNOHANG = 1;

/**
 * Signal when a process cannot be found.
 * @type {number}
 */
NaClTerm.ENOENT = 2;

/**
 * Error when a process does not have unwaited-for children.
 * @type {number}
 */
NaClTerm.ECHILD= 10;

/**
 * Flag for cyan coloring in the terminal.
 * @const
 */
NaClTerm.ANSI_CYAN = '\x1b[36m';

/**
 * Flag for color reset in the terminal.
 * @const
 */
NaClTerm.ANSI_RESET = '\x1b[0m';

// TODO(bradnelson): Rename in line with style guide once we have tests.
// The process which gets the input from the user.
var foreground_process;
// Process information keyed by PID. The value is an embed DOM object
// if the process is running. Once the process has finished, the value
// will be the exit code.
var processes = {};

// Waiter processes keyed by the PID of the waited process. The waiter
// is represented by a hash like
// { element: embed DOM object, wait_req_id: the request ID string }
var waiters = {};
var pid = 0;

/**
 * Static initialier called from index.html.
 *
 * This constructs a new Terminal instance and instructs it to run the NaClTerm
 * command.
 */
NaClTerm.init = function() {
  var profileName = lib.f.parseQuery(document.location.search)['profile'];
  var terminal = new hterm.Terminal(profileName);
  terminal.decorate(document.querySelector('#terminal'));

  // Useful for console debugging.
  window.term_ = terminal;

  // We don't properly support the hterm bell sound, so we need to disable it.
  terminal.prefs_.definePreference('audible-bell-sound', '');

  terminal.setAutoCarriageReturn(true);
  terminal.setCursorPosition(0, 0);
  terminal.setCursorVisible(true);
  terminal.runCommandClass(NaClTerm, document.location.hash.substr(1));

  return true;
};

/**
 * Makes the path in a NMF entry to fully specified path.
 *
 * @private
 */
NaClTerm.prototype.adjustNmfEntry_ = function(entry) {
  for (var arch in entry) {
    var path = entry[arch]['url'];
    var html5_mount_point = '/mnt/html5';
    if (path.indexOf(html5_mount_point) == 0) {
      path = path.replace(html5_mount_point,
                          'filesystem:' + location.origin + '/persistent');
    } else {
      // This is for the dynamic loader.
      var base = location.href.match('.*/')[0];
      path = base + path;
    }
    entry[arch]['url'] = path;
  }
}

/**
 * Handle messages sent to us from NaCl.
 *
 * @private
 */
NaClTerm.prototype.handleMessage_ = function(e) {
  if (e.data['command'] == 'nacl_spawn') {
    var msg = e.data;
    console.log('nacl_spawn: ' + JSON.stringify(msg));
    var args = msg['args'];
    var envs = msg['envs'];
    var cwd = msg['cwd'];
    var executable = args[0];
    var nmf = msg['nmf'];
    if (nmf) {
      if (nmf['files']) {
        for (var key in nmf['files'])
          this.adjustNmfEntry_(nmf['files'][key]);
      }
      this.adjustNmfEntry_(nmf['program']);
      var blob = new Blob([JSON.stringify(nmf)], {type: 'text/plain'});
      nmf = window.URL.createObjectURL(blob);
    } else {
      if (NaClTerm.nmfWhitelist !== undefined &&
          NaClTerm.nmfWhitelist.indexOf(executable) === -1) {
        var reply = {};
        reply[e.data['id']] = {
          pid: -NaClTerm.ENOENT,
        };
        console.log('nacl_spawn(error): ' + JSON.stringify(reply));
        e.srcElement.postMessage(reply);
        return;
      }
      nmf = executable + '.nmf';
    }
    this.spawn(nmf, args, envs, cwd, executable, e);
  } else if (e.data['command'] == 'nacl_wait') {
    var msg = e.data;
    console.log('nacl_wait: ' + JSON.stringify(msg));
    this.waitpid(msg['pid'], msg['options'], e);
  } else if (e.data.indexOf(NaClTerm.prefix) == 0) {
    var msg = e.data.substring(NaClTerm.prefix.length);
    if (!this.loaded) {
      this.bufferedOutput += msg;
    } else {
      this.io.print(msg);
    }
  } else if (e.data.indexOf('exited') == 0) {
    var exitCode = parseInt(e.data.split(':', 2)[1]);
    if (isNaN(exitCode))
      exitCode = 0;
    this.exit(exitCode, e.srcElement);
  } else {
    console.log('unexpected message: ' + e.data);
    return;
  }
}

/**
 * Handle load error event from NaCl.
 */
NaClTerm.prototype.handleLoadAbort_ = function(e) {
  this.io.print('Load aborted.\n');
}

/**
 * Handle load abort event from NaCl.
 */
NaClTerm.prototype.handleLoadError_ = function(e) {
  console.log('load error: ' + e.srcElement.spawn_req_id);
  if (e.srcElement.spawn_req_id) {
    var reply = {};
    reply[e.srcElement.spawn_req_id] = {
      pid: -NaClTerm.ENOENT,
    };
    console.log('handleLoadError: ' + JSON.stringify(reply));
    e.srcElement.parent.postMessage(reply);
    foreground_process = e.srcElement.parent;
  }

  this.io.print(e.srcElement.command_name + ': ' +
                e.srcElement.lastError + '\n');
  document.body.removeChild(e.srcElement);
}

NaClTerm.prototype.doneLoadingUrl = function() {
  var width = this.io.terminal_.screenSize.width;
  this.io.print('\r' + Array(width+1).join(' '));
  var message = '\rLoaded ' + this.lastUrl;
  if (this.lastTotal) {
    var kbsize = Math.round(this.lastTotal/1024)
    message += ' ['+ kbsize + ' KiB]';
  }
  this.io.print(message.slice(0, width) + '\n')
}

/**
 * Handle load end event from NaCl.
 */
NaClTerm.prototype.handleLoad_ = function(e) {
  if (e.srcElement.spawn_req_id) {
    var reply = {};
    reply[e.srcElement.spawn_req_id] = {
      pid: e.srcElement.pid,
    };
    console.log('handleLoad: ' + JSON.stringify(reply));
    e.srcElement.parent.postMessage(reply);
  }

  // Don't print loading messages, except for the
  // root process.
  if (!e.srcElement.parent) {
    if (this.lastUrl)
      this.doneLoadingUrl();
    else
      this.io.print('Loaded.\n');

    this.io.print(NaClTerm.ANSI_RESET);
  }

  // Now that have completed loading and displaying
  // loading messages we output any messages from the
  // NaCl module that were buffered up unto this point
  this.loaded = true;
  this.io.print(this.bufferedOutput);
  this.bufferedOutput = ''
}

/**
 * Handle load progress event from NaCl.
 */
NaClTerm.prototype.handleProgress_ = function(e) {
  if (e.url !== undefined)
    var url = e.url.substring(e.url.lastIndexOf('/') + 1);

  if (!e.srcElement.parent && this.lastUrl && this.lastUrl != url)
    this.doneLoadingUrl()

  if (!url)
    return;

  this.lastUrl = url;
  this.lastTotal = e.total;

  if (e.srcElement.parent)
    return;

  var message = 'Loading ' + url;
  if (e.lengthComputable && e.total) {
    var percent = Math.round(e.loaded * 100 / e.total);
    var kbloaded = Math.round(e.loaded / 1024);
    var kbtotal = Math.round(e.total / 1024);
    message += ' [' + kbloaded + ' KiB/' + kbtotal + ' KiB ' + percent + '%]';
  }

  var width = this.io.terminal_.screenSize.width;
  this.io.print('\r' + message.slice(-width));
}

/**
 * Handle crash event from NaCl.
 */
NaClTerm.prototype.handleCrash_ = function(e) {
  this.exit(e.srcElement.exitStatus, e.srcElement);
}

/**
 * Exit the command.
 */
NaClTerm.prototype.exit = function(code, element) {
  if (!element.parent) {
    this.io.print(NaClTerm.ANSI_CYAN)

    // The root process finished.
    if (code == -1) {
      this.io.print('Program (' + element.command_name +
                    ') crashed (exit status -1)\n');
    } else {
      this.io.print('Program (' + element.command_name + ') exited ' +
                    '(status=' + code + ')\n');
    }

    this.io.pop();
    if (this.argv_.onExit)
      this.argv_.onExit(code);
    return;
  }

  var pid = element.pid;
  function wakeWaiters(waiters) {
    for (var i = 0; i < waiters.length; i++) {
      var waiter = waiters[i];
      var reply = {};
      reply[waiter.wait_req_id] = {
        pid: pid,
        status: code,
      };
      console.log('exit: ' + JSON.stringify(reply));
      waiter.element.postMessage(reply);
      waiter = null;
    }
  }
  if (waiters[pid] || waiters[-1]) {
    if (waiters[pid]) {
      wakeWaiters(waiters[pid]);
      delete waiters[pid];
    }
    if (waiters[-1]) {
      wakeWaiters(waiters[-1]);
      delete waiters[-1];
    }

    delete processes[pid];
  } else {
    processes[pid] = code;
  }

  // Mark as terminated.
  element.pid = -1;
  var next_foreground_process = null;
  if (foreground_process == element) {
    next_foreground_process = element.parent;
    // When the parent has already finished, give the control to the
    // grand parent.
    while (next_foreground_process.pid == -1)
      next_foreground_process = next_foreground_process.parent;
  }
  document.body.removeChild(element);
  if (next_foreground_process)
    foreground_process = next_foreground_process;
  return;
};

/**
 * Create the NaCl embed element.
 * We delay this until the first terminal resize event so that we start
 * with the correct size.
 */
NaClTerm.prototype.createEmbed = function(nmf, argv, envs, cwd,
                                          width, height) {
  var mimetype = 'application/x-pnacl';
  if (navigator.mimeTypes[mimetype] === undefined) {
    if (mimetype.indexOf('pnacl') != -1)
      this.io.print('Browser does not support PNaCl or PNaCl is disabled\n');
    else
      this.io.print('Browser does not support NaCl or NaCl is disabled\n');
    return;
  }

  ++pid;
  foreground_process = document.createElement('object');
  foreground_process.pid = pid;
  foreground_process.width = 0;
  foreground_process.height = 0;
  foreground_process.data = nmf;
  foreground_process.type = mimetype;
  foreground_process.addEventListener(
      'message', this.handleMessage_.bind(this));
  foreground_process.addEventListener(
      'progress', this.handleProgress_.bind(this));
  foreground_process.addEventListener('load', this.handleLoad_.bind(this));
  foreground_process.addEventListener(
      'error', this.handleLoadError_.bind(this));
  foreground_process.addEventListener(
      'abort', this.handleLoadAbort_.bind(this));
  foreground_process.addEventListener('crash', this.handleCrash_.bind(this));
  processes[pid] = foreground_process;

  var params = {};
  for (var i = 0; i < envs.length; i++) {
    var env = envs[i];
    var index = env.indexOf('=');
    if (index < 0) {
      console.error('Broken env: ' + env);
      continue;
    }
    var key = env.substring(0, index);
    if (key == 'SRC' || key == 'DATA' || key.match(/^ARG\d+$/i))
      continue;
    params[key] = env.substring(index + 1);
  }

  params['PS_TTY_PREFIX'] = NaClTerm.prefix;
  params['PS_TTY_RESIZE'] = 'tty_resize';
  params['PS_TTY_COLS'] = width ? width : this.tty_width;
  params['PS_TTY_ROWS'] = height ? height : this.tty_height;
  params['PS_STDIN'] = '/dev/tty';
  params['PS_STDOUT'] = '/dev/tty';
  params['PS_STDERR'] = '/dev/tty';
  params['PS_VERBOSITY'] = '2';
  params['PS_EXIT_MESSAGE'] = 'exited';
  params['TERM'] = 'xterm-256color';
  params['PWD'] = cwd;
  // TODO(bradnelson): Drop this hack once tar extraction first checks relative
  // to the nexe.
  if (NaClTerm.useNaClAltHttp === true) {
    params['NACL_ALT_HTTP'] = '1';
  }

  function addParam(name, value) {
    var param = document.createElement('param');
    param.name = name;
    param.value = value;
    foreground_process.appendChild(param);
  }

  for (var key in params) {
    addParam(key, params[key]);
  }

  // Add ARGV arguments from query parameters.
  var args = lib.f.parseQuery(document.location.search);
  for (var argname in args) {
    addParam(argname, args[argname]);
  }

  // If the application has set NaClTerm.argv and there were
  // no arguments set in the query parameters then add the default
  // NaClTerm.argv arguments.
  if (args['arg1'] === undefined && argv) {
    var argn = 0;
    argv.forEach(function(arg) {
      var argname = 'arg' + argn;
      addParam(argname, arg);
      argn = argn + 1
    })
  }

  // We show this message only for the first process.
  if (pid == 1)
    this.io.print('Loading NaCl module.\n');
  document.body.appendChild(foreground_process);
}

NaClTerm.prototype.spawn = function(nmf, argv, envs, cwd,
                                    command_name, e) {
  this.createEmbed(nmf, argv, envs, cwd);
  foreground_process.parent = e.srcElement;
  foreground_process.command_name = command_name;
  foreground_process.spawn_req_id = e.data['id'];
}

NaClTerm.prototype.waitpid = function(pid, options, e) {
  var finishedProcess = null;
  Object.keys(processes).some(function(pid) {
    if (typeof processes[pid] == 'number') {
      finishedProcess = pid;
      return true;
    }
    return false;
  });

  if (pid == -1 && finishedProcess !== null) {
    var reply = {};
    reply[e.data['id']] = {
      pid: parseInt(finishedProcess),
      status: processes[finishedProcess],
    };
    delete processes[finishedProcess];
    e.srcElement.postMessage(reply);
  } else if (pid >= 0 && !processes[pid]) {
    // The process does not exist.
    var reply = {};
    reply[e.data['id']] = {
      pid: -NaClTerm.ECHILD,
    };
    console.log('waitpid (ECHILD): ' + JSON.stringify(reply));
    e.srcElement.postMessage(reply);
  } else if (typeof processes[pid] == 'number') {
    // The process has already finished.
    var reply = {};
    reply[e.data['id']] = {
      pid: pid,
      status: processes[pid],
    };

    delete processes[pid];

    console.log('waitpid: ' + JSON.stringify(reply));
    e.srcElement.postMessage(reply);
  } else {
    // Add the current process to the waiter list.
    if (options & this.WNOHANG != 0) {
      var reply = {};
      reply[e.data['id']] = {
        pid: 0,
        status: 0,
      };
      e.srcElement.postMessage(reply);
      return;
    }
    if (!waiters[pid]) {
      waiters[pid] = [];
    }
    waiters[pid].push({
      element: e.srcElement,
      wait_req_id: e.data['id'],
      options: options
    });
  }
}

NaClTerm.prototype.onTerminalResize_ = function(width, height) {
  this.tty_width = width;
  this.tty_height = height;
  if (foreground_process === undefined) {
    var argv = NaClTerm.argv || [];
    argv = [NaClTerm.nmf].concat(argv);
    this.createEmbed(NaClTerm.nmf, argv, [], '/', width, height);
    // The root process has no parent.
    foreground_process.parent = null;
    foreground_process.command_name = NaClTerm.prefix;
  } else {
    foreground_process.postMessage({'tty_resize': [ width, height ]});
  }
}

NaClTerm.prototype.onVTKeystroke_ = function(str) {
  var message = {};
  message[NaClTerm.prefix] = str;
  foreground_process.postMessage(message);
}

/*
 * This is invoked by the terminal as a result of terminal.runCommandClass().
 */
NaClTerm.prototype.run = function() {
  this.bufferedOutput = '';
  this.loaded = false;
  this.io.print(NaClTerm.ANSI_CYAN);

  this.io.onVTKeystroke = this.onVTKeystroke_.bind(this);
  this.io.onTerminalResize = this.onTerminalResize_.bind(this);
};
