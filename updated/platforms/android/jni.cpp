#include "../../src/live.hpp"
#include <android/log.h>
#include <jni.h>
#include <memory>
struct NativeState {
  std::unique_ptr<pf::LiveFusion> fusion;
  std::string error;
};
static std::string text(JNIEnv *e, jstring s) {
  const char *p = e->GetStringUTFChars(s, nullptr);
  std::string t = p;
  e->ReleaseStringUTFChars(s, p);
  return t;
}
extern "C" JNIEXPORT jlong JNICALL
Java_io_github_twerdster_mobilepointfusion_FusionActivity_create(
    JNIEnv *e, jclass, jstring root, jstring output) {
  auto *state = new NativeState;
  try {
    state->fusion =
        std::make_unique<pf::LiveFusion>(text(e, root), text(e, output));
  } catch (const std::exception &ex) {
    state->error = ex.what();
    __android_log_print(ANDROID_LOG_ERROR, "MobilePointFusion", "%s",
                        ex.what());
  }
  return reinterpret_cast<jlong>(state);
}
extern "C" JNIEXPORT jstring JNICALL
Java_io_github_twerdster_mobilepointfusion_FusionActivity_draw(
    JNIEnv *e, jclass, jlong handle, jint w, jint h, jboolean step,
    jfloat orbit) {
  auto *s = reinterpret_cast<NativeState *>(handle);
  std::string result = "Native context unavailable";
  if (s) {
    try {
      if (s->fusion && s->error.empty()) {
        auto &f = *s->fusion;
        if (step)
          f.step();
        if (w > 0 && h > 0)
          f.render(0, w, h, orbit);
        result = "View " + std::to_string(f.frame) + " / " +
                 std::to_string(f.frames) + " · " +
                 std::to_string(f.model.size()) + " points\n" +
                 std::to_string(f.model.merged) + " merges · " +
                 std::to_string(f.model.removed) + " removals\n" +
                 std::to_string(f.compared) +
                 " fusion correspondences checked against CPU";
      }
    } catch (const std::exception &ex) {
      s->error = ex.what();
      __android_log_print(ANDROID_LOG_ERROR, "MobilePointFusion", "%s",
                          ex.what());
    }
    if (!s->error.empty())
      result = "Execution failed: " + s->error;
  }
  return e->NewStringUTF(result.c_str());
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_twerdster_mobilepointfusion_FusionActivity_reset(JNIEnv *,
                                                                jclass,
                                                                jlong handle) {
  auto *s = reinterpret_cast<NativeState *>(handle);
  if (s && s->fusion) {
    try {
      s->fusion->reset();
      s->error.clear();
    } catch (const std::exception &e) {
      s->error = e.what();
    }
  }
}
extern "C" JNIEXPORT void JNICALL
Java_io_github_twerdster_mobilepointfusion_FusionActivity_destroy(
    JNIEnv *, jclass, jlong handle, jboolean context_lost) {
  auto *s = reinterpret_cast<NativeState *>(handle);
  if (s && s->fusion && context_lost)
    s->fusion->abandon_context();
  delete s;
}
