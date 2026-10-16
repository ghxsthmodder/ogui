#pragma once
// ------------------------------------------------------------------
// The Java half of the input connection, in four functions and no JNI type
// ------------------------------------------------------------------
// An input connection is a Java object, so the half of the IME that commits
// text, deletes around a cursor and reports where the caret is cannot live in
// the library. It lives in android/src/dev/ogui/android, and this is the seam:
// the host hands the bridge over when the activity is created, asks for a
// keyboard, and never looks at the JVM itself.
//
// The definitions are in src/androidjni.cpp, which compiles to nothing off
// Android, and the calls are inside the Android half of src/androidhost.cpp --
// which is why no platform has a stub to keep in step.
#include "ogui/androidhost.hpp"

namespace ogui {

// The bridge the IME's edits are queued into. One per process, because
// NativeActivity gives one activity per process; the host attaches its own in
// create() and detaches it in destroy().
void androidImeAttach(AndroidInputBridge* bridge);
void androidImeDetach();

// True when the Java class was found and its natives registered -- which says
// nothing about whether it has an editor view yet, and is not meant to: a host
// without one falls back to the NDK's own keyboard call.
bool androidImeAvailable();

// Hand the request to the Java side. False when it is not ready -- the activity
// has not created its editor view, or the JVM refused the call -- which is the
// host's cue to try again on the next frame rather than to have asked.
bool androidImeSendRequest(const ImeRequest& request);

} // namespace ogui
