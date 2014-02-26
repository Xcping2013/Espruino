/*
 * This file is part of Espruino, a JavaScript interpreter for Microcontrollers
 *
 * Copyright (C) 2013 Gordon Williams <gw@pur3.co.uk>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * ----------------------------------------------------------------------------
 * This file is designed to be parsed during the build process
 *
 * JavaScript methods for Waveforms (eg. Audio)
 * ----------------------------------------------------------------------------
 */
#include "jswrap_waveform.h"
#include "jswrap_arraybuffer.h"
#include "jsvar.h"
#include "jsparse.h"
#include "jsinteractive.h"
#include "jstimer.h"

#define JSI_WAVEFORM_NAME JS_HIDDEN_CHAR_STR"wave"


/*JSON{ "type":"class", "ifndef" : "SAVE_ON_FLASH",
        "class" : "Waveform",
        "description" : [ "This class handles waveforms. In Espruino, a Waveform is a set of data that you want to input or output." ]
}*/

static JsVar *jswrap_waveform_getBuffer(JsVar *waveform, int bufferNumber) {
  JsVar *buffer = jsvObjectGetChild(waveform, (bufferNumber==0)?"buffer":"buffer2", 0);
  // plough through to get array buffer data
  while (jsvIsArrayBuffer(buffer)) {
    JsVar *s = jsvLock(buffer->firstChild);
    jsvUnLock(buffer);
    buffer = s;
  }
  assert(jsvIsUndefined(buffer) || jsvIsString(buffer));
  return buffer;
}


/*JSON{ "type":"idle", "generate" : "jswrap_waveform_idle", "ifndef" : "SAVE_ON_FLASH" }*/
bool jswrap_waveform_idle() {
  JsVar *waveforms = jsvObjectGetChild(execInfo.root, JSI_WAVEFORM_NAME, 0);
  if (waveforms) {
    JsvArrayIterator it;
    jsvArrayIteratorNew(&it, waveforms);
    while (jsvArrayIteratorHasElement(&it)) {
      JsVar *waveform = jsvArrayIteratorGetElement(&it);

      bool running = jsvGetBoolAndUnLock(jsvObjectGetChild(waveform, "running", 0));
      if (running) {
        JsVar *buffer = jswrap_waveform_getBuffer(waveform,0);
        UtilTimerTask task;
        // Search for a timer task
        if (!jstGetLastBufferTimerTask(buffer, &task)) {
          // if the timer task is now gone...
          JsVar *arrayBuffer = jsvObjectGetChild(waveform, "buffer", 0);
          jsiQueueObjectCallbacks(waveform, "#onfinish", arrayBuffer, 0);
          jsvUnLock(arrayBuffer);
          running = false;
          jsvUnLock(jsvObjectSetChild(waveform, "running", jsvNewFromBool(running)));
        } else {
          // If the timer task is still there...
          if (task.data.buffer.nextBuffer &&
              task.data.buffer.nextBuffer != task.data.buffer.currentBuffer) {
            // if it is a double-buffered task
            int currentBuffer = (jsvGetRef(buffer)==task.data.buffer.currentBuffer) ? 0 : 1;
            JsVar *oldBuffer = jsvObjectGetChild(waveform, "currentBuffer", JSV_INTEGER);
            if (jsvGetInteger(oldBuffer) !=currentBuffer) {
              // buffers have changed - fire off a 'buffer' event with the buffer that needs to be filled
              jsvSetInteger(oldBuffer, currentBuffer);
              JsVar *arrayBuffer = jsvObjectGetChild(waveform, (currentBuffer==0) ? "buffer" : "buffer2", 0);
              jsiQueueObjectCallbacks(waveform, "#onbuffer", arrayBuffer, 0);
              jsvUnLock(arrayBuffer);
            }
            jsvUnLock(oldBuffer);
          }
        }
        jsvUnLock(buffer);
      }
      jsvUnLock(waveform);
      // if not running, remove waveform from this list
      if (!running)
        jsvArrayIteratorRemoveAndGotoNext(&it, waveforms);
      else
        jsvArrayIteratorNext(&it);
    }
    jsvArrayIteratorFree(&it);
    jsvUnLock(waveforms);
  }
  return false; // no need to stay awake - an IRQ will wake us
}

/*JSON{ "type":"kill", "generate" : "jswrap_waveform_kill", "ifndef" : "SAVE_ON_FLASH" }*/
void jswrap_waveform_kill() { // be sure to remove all waveforms...
  JsVar *waveforms = jsvObjectGetChild(execInfo.root, JSI_WAVEFORM_NAME, 0);
  if (waveforms) {
    JsvArrayIterator it;
    jsvArrayIteratorNew(&it, waveforms);
    while (jsvArrayIteratorHasElement(&it)) {
      JsVar *waveform = jsvArrayIteratorGetElement(&it);
      bool running = jsvGetBoolAndUnLock(jsvObjectGetChild(waveform, "running", 0));
      if (running) {
        JsVar *buffer = jswrap_waveform_getBuffer(waveform,0);
        if (!jstStopBufferTimerTask(buffer)) {
          jsError("Waveform couldn't be stopped");
        }
        jsvUnLock(buffer);
      }
      jsvUnLock(waveform);
      // if not running, remove waveform from this list
      jsvArrayIteratorRemoveAndGotoNext(&it, waveforms);
    }
    jsvArrayIteratorFree(&it);
    jsvUnLock(waveforms);
  }
}


/*JSON{ "type":"constructor", "class": "Waveform",  "name": "Waveform", "ifndef" : "SAVE_ON_FLASH",
         "description" : [ "Create a waveform class. This allows high speed input and output of waveforms. It has an internal variable called `buffer` (as well as `buffer2` when double-buffered - see `options` below) which contains the data to input/output.",
                           "When double-buffered, a 'buffer' event will be emitted each time a buffer is finished with (the argument is that buffer). When the recording stops, a 'finish' event will be emitted (with the first argument as the buffer)." ],
         "generate" : "jswrap_waveform_constructor",
         "params" : [ [ "samples", "int32", "The number of samples" ],
                      [ "options", "JsVar", "Optional options struct `{doubleBuffer:bool}` where: `doubleBuffer` is whether to allocate two buffers or not." ] ],
         "return" : [ "JsVar", "An Waveform object" ]

}*/
JsVar *jswrap_waveform_constructor(int samples, JsVar *options) {
  if (samples<=0) {
    jsError("samples must be greater than 0");
    return 0;
  }

  bool doubleBuffer = false;
  if (jsvIsObject(options)) {
    doubleBuffer = jsvGetBoolAndUnLock(jsvObjectGetChild(options, "doubleBuffer", 0));
  } else if (!jsvIsUndefined(options)) {
    jsError("Expecting options to be undefined or an Object, not %t", options);
  }

  JsVar *arrayLength = jsvNewFromInteger(samples);
  JsVar *arrayBuffer = jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT8, arrayLength, 0, 0);
  JsVar *arrayBuffer2 = 0;
  if (doubleBuffer) arrayBuffer2 = jswrap_typedarray_constructor(ARRAYBUFFERVIEW_UINT8, arrayLength, 0, 0);
  jsvUnLock(arrayLength);
  JsVar *waveform = jspNewObject(0, "Waveform");


  if (!waveform || !arrayBuffer || (doubleBuffer && !arrayBuffer2)) {
    jsvUnLock(waveform);
    jsvUnLock(arrayBuffer); // out of memory
    jsvUnLock(arrayBuffer2);
    return 0;
  }
  jsvUnLock(jsvObjectSetChild(waveform, "buffer", arrayBuffer));
  if (arrayBuffer2) jsvUnLock(jsvObjectSetChild(waveform, "buffer2", arrayBuffer2));

  return waveform;
}

static void jswrap_waveform_start(JsVar *waveform, Pin pin, JsVarFloat freq, JsVar *options, UtilTimerEventType eventType) {
  bool running = jsvGetBoolAndUnLock(jsvObjectGetChild(waveform, "running", 0));
  if (running) {
    jsError("Waveform is already running");
    return;
  }
  if (!jshIsPinValid(pin)) {
    jsError("Invalid pin");
    return;
  }
  if (!isfinite(freq) || freq<1) {
    jsError("Frequency must be above 1Hz");
    return;
  }

  JsSysTime startTime = jshGetSystemTime();
  bool repeat = false;
  if (jsvIsObject(options)) {
    JsVarFloat t = jsvGetFloatAndUnLock(jsvObjectGetChild(options, "time", 0));
    if (!isfinite(t) && t>0)
       startTime = jshGetTimeFromMilliseconds(t/1000);
    repeat = jsvGetBoolAndUnLock(jsvObjectGetChild(options, "repeat", 0));
  } else if (!jsvIsUndefined(options)) {
    jsError("Expecting options to be undefined or an Object, not %t", options);
  }

  JsVar *buffer = jswrap_waveform_getBuffer(waveform,0);
  JsVar *buffer2 = jswrap_waveform_getBuffer(waveform,1);
  // And finally set it up
  if (!jstStartSignal(startTime, jshGetTimeFromMilliseconds(1000.0 / freq), pin, buffer, repeat?(buffer2?buffer2:buffer):0, eventType))
    jsWarn("Unable to schedule a timer");
  jsvUnLock(buffer);
  jsvUnLock(buffer2);

  jsvUnLock(jsvObjectSetChild(waveform, "running", jsvNewFromBool(true)));
  jsvUnLock(jsvObjectSetChild(waveform, "freq", jsvNewFromFloat(freq)));
  // Add to our list of active waveforms
  JsVar *waveforms = jsvObjectGetChild(execInfo.root, JSI_WAVEFORM_NAME, JSV_ARRAY);
  if (waveforms) {
    jsvArrayPush(waveforms, waveform);
    jsvUnLock(waveforms);
  }
}

/*JSON{ "type":"method", "class": "Waveform", "name" : "startOutput", "ifndef" : "SAVE_ON_FLASH",
         "description" : "Will start outputting the waveform on the given pin - the pin must have previously been initialised with analogWrite. If not repeating, it'll emit a `finish` event when it is done.",
         "generate" : "jswrap_waveform_startOutput",
         "params" : [ [ "output", "pin", "The pin to output on" ],
                      [ "freq", "float", "The frequency to output each sample at"],
                      [ "options", "JsVar", "Optional options struct `{time:float,repeat:bool}` where: `time` is the that the waveform with start output at, e.g. `getTime()+1` (otherwise it is immediate), `repeat` is a boolean specifying whether to repeat the give sample"] ]
}*/
void jswrap_waveform_startOutput(JsVar *waveform, Pin pin, JsVarFloat freq, JsVar *options) {
  jswrap_waveform_start(waveform, pin, freq, options, UET_WRITE_BYTE);
}

/*JSON{ "type":"method", "class": "Waveform", "name" : "startInput", "ifndef" : "SAVE_ON_FLASH",
         "description" : "Will start inputting the waveform on the given pin that supports analog. If not repeating, it'll emit a `finish` event when it is done.",
         "generate" : "jswrap_waveform_startInput",
         "params" : [ [ "output", "pin", "The pin to output on" ],
                      [ "freq", "float", "The frequency to output each sample at"],
                      [ "options", "JsVar", "Optional options struct `{time:float,repeat:bool}` where: `time` is the that the waveform with start output at, e.g. `getTime()+1` (otherwise it is immediate), `repeat` is a boolean specifying whether to repeat the give sample"] ]
}*/
void jswrap_waveform_startInput(JsVar *waveform, Pin pin, JsVarFloat freq, JsVar *options) {
  // Setup analog, and also bail out on failure
  if (jshPinAnalog(pin)<0) return;
  // start!
  jswrap_waveform_start(waveform, pin, freq, options, UET_READ_BYTE);
}

/*JSON{ "type":"method", "class": "Waveform", "name" : "stop", "ifndef" : "SAVE_ON_FLASH",
         "description" : "Stop a waveform that is currently outputting",
         "generate" : "jswrap_waveform_stop"
}*/
void jswrap_waveform_stop(JsVar *waveform) {
  bool running = jsvGetBoolAndUnLock(jsvObjectGetChild(waveform, "running", 0));
  if (!running) {
    jsError("Waveform is not running");
    return;
  }
  JsVar *buffer = jswrap_waveform_getBuffer(waveform,0);
  if (!jstStopBufferTimerTask(buffer)) {
    jsError("Waveform couldn't be stopped");
  }
  jsvUnLock(buffer);
  // now run idle loop as this will issue the finish event and will clean up
  jswrap_waveform_idle();
}

