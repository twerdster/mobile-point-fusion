package io.github.twerdster.mobilepointfusion;

import android.app.Activity;
import android.graphics.Color;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.view.MotionEvent;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * New native UI; the GLSurfaceView context executes ES3 shaders and C++
 * fusion.
 */
public final class FusionActivity extends Activity {
  static { System.loadLibrary("fusion_native"); }
  private static native long create(String root, String output);
  private static native String draw(long handle, int width, int height,
                                    boolean step, float orbit);
  private static native void reset(long handle);
  private static native void destroy(long handle, boolean contextLost);
  private GLSurfaceView surface;
  private TextView status;
  private volatile boolean running = true;
  private volatile float orbit = 0;
  private long handle = 0;
  private float previousX;

  private TextView label(String text, float size) {
    TextView v = new TextView(this);
    v.setText(text);
    v.setTextColor(Color.WHITE);
    v.setTextSize(size);
    v.setPadding(12, 4, 12, 4);
    return v;
  }
  private void copyAsset(String path) throws Exception {
    File dst = new File(getFilesDir(), "fusion/" + path);
    dst.getParentFile().mkdirs();
    try (InputStream in = getAssets().open("fusion/" + path);
         FileOutputStream out = new FileOutputStream(dst)) {
      byte[] buffer = new byte[8192];
      int n;
      while ((n = in.read(buffer)) != -1)
        out.write(buffer, 0, n);
    }
  }
  @Override
  public void onCreate(Bundle state) {
    super.onCreate(state);
    LinearLayout layout = new LinearLayout(this);
    layout.setOrientation(LinearLayout.VERTICAL);
    layout.setBackgroundColor(Color.rgb(6, 11, 20));
    layout.setPadding(10, 14, 10, 12);
    layout.addView(label("Mobile Point Fusion", 25));
    layout.addView(label("On-device ES3 shader + CPU execution", 16));
    layout.addView(label(
        "Generated input · supplied poses · no real RGB-D or tracking", 12));
    LinearLayout headings = new LinearLayout(this);
    headings.addView(label("CURRENT OBSERVATION", 11),
                     new LinearLayout.LayoutParams(0, -2, 1));
    headings.addView(label("ACCUMULATED MODEL", 11),
                     new LinearLayout.LayoutParams(0, -2, 1));
    layout.addView(headings);
    status = label("Preparing native ES3 context…", 12);
    try {
      for (String path : new String[] {
               "source/original/PointsFusion/PointsFusion/indexMap.vsh",
               "source/original/PointsFusion/PointsFusion/indexMap.fsh",
               "updated/shaders/Fusion.vsh", "updated/shaders/Fusion.fsh",
               "updated/shaders/PointRemoval.vsh",
               "updated/shaders/PointRemoval.fsh"})
        copyAsset(path);
    } catch (Exception e) {
      layout.addView(label("Resource failure: " + e, 14));
      setContentView(layout);
      return;
    }
    surface = new GLSurfaceView(this);
    surface.setEGLContextClientVersion(3);
    surface.setPreserveEGLContextOnPause(true);
    surface.setRenderer(new GLSurfaceView.Renderer() {
      int width, height;
      long last;
      @Override
      public void onSurfaceCreated(GL10 unused, EGLConfig config) {
        if (handle != 0)
          destroy(handle, true);
        handle =
            create(new File(getFilesDir(), "fusion").getAbsolutePath(),
                   new File(getFilesDir(), "native-output").getAbsolutePath());
      }
      @Override
      public void onSurfaceChanged(GL10 unused, int w, int h) {
        width = w;
        height = h;
      }
      @Override
      public void onDrawFrame(GL10 unused) {
        long now = System.nanoTime();
        boolean step = running && now - last > 150_000_000;
        String text = draw(handle, width, height, step, orbit);
        if (step)
          last = now;
        runOnUiThread(() -> status.setText(text));
      }
    });
    surface.setOnTouchListener((view, event) -> {
      if (event.getActionMasked() == MotionEvent.ACTION_DOWN)
        previousX = event.getX();
      if (event.getActionMasked() == MotionEvent.ACTION_MOVE) {
        orbit += (event.getX() - previousX) / 180.f;
        previousX = event.getX();
      }
      return true;
    });
    layout.addView(surface, new LinearLayout.LayoutParams(-1, 0, 1));
    layout.addView(status);
    LinearLayout controls = new LinearLayout(this);
    Button pause = new Button(this);
    pause.setText("Pause");
    pause.setOnClickListener(v -> {
      running = !running;
      pause.setText(running ? "Pause" : "Resume");
    });
    controls.addView(pause);
    Button reset = new Button(this);
    reset.setText("Reset and run");
    reset.setOnClickListener(v -> {
      surface.queueEvent(() -> {
        if (handle != 0)
          reset(handle);
      });
      orbit = 0;
      running = true;
      pause.setText("Pause");
    });
    controls.addView(reset);
    layout.addView(controls);
    layout.addView(label(
        "Drag to orbit. Every view executes correspondence, weighted fusion and overlap removal on this device.",
        11));
    layout.addView(label(
        "2015: D. Ben-Hoda, G. Kamar, A. Wetzler, Y. Honen, R. Kimmel\n2026 adapter · Daniel & Gal notices preserved",
        10));
    setContentView(layout);
  }
  @Override
  protected void onPause() {
    if (surface != null) {
      if (isFinishing() || isChangingConfigurations())
        surface.queueEvent(() -> {
          if (handle != 0) {
            destroy(handle, false);
            handle = 0;
          }
        });
      surface.onPause();
    }
    super.onPause();
  }
  @Override
  protected void onResume() {
    super.onResume();
    if (surface != null)
      surface.onResume();
  }
}
