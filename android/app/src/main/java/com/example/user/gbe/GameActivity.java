package com.example.user.gbe;

import android.annotation.SuppressLint;
import android.content.Intent;
import android.graphics.Canvas;
import android.support.v7.app.ActionBar;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.os.Handler;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.widget.Toast;

/**
 * An example full-screen activity that shows and hides the system UI (i.e.
 * status bar and navigation/system bar) with user interaction.
 */
public class GameActivity extends AppCompatActivity {
    /**
     * Whether or not the system UI should be auto-hidden after
     * {@link #AUTO_HIDE_DELAY_MILLIS} milliseconds.
     */
    private static final boolean AUTO_HIDE = true;

    /**
     * If {@link #AUTO_HIDE} is set, the number of milliseconds to wait after
     * user interaction before hiding the system UI.
     */
    private static final int AUTO_HIDE_DELAY_MILLIS = 3000;

    /**
     * Some older devices needs a small delay between UI widget updates
     * and a change of the status and navigation bar.
     */
    private static final int UI_ANIMATION_DELAY = 300;

    private View mContentView;
    private boolean mToolbarVisible;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        setContentView(R.layout.activity_game);

        mToolbarVisible = true;
        mContentView = findViewById(R.id.fullscreen_content);

        // Set up the user interaction to manually showToolbar or hideToolbar the system UI.
        mContentView.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                // re-hideToolbar the controls
                if (mToolbarVisible)
                    hideToolbar();
            }
        });

        // Pull parameters back from launcher.
        Intent intent = getIntent();
        String romPath = (intent != null) ? intent.getStringExtra("romPath") : null;
        if (intent == null || romPath == null) {
            // incomplete launch
            Toast.makeText(GameActivity.this, "Invalid launch parameters.", Toast.LENGTH_SHORT).show();
            finish();
        }
    }

    @Override
    protected void onPostCreate(Bundle savedInstanceState) {
        super.onPostCreate(savedInstanceState);

        // Trigger the initial hideToolbar() shortly after the activity has been
        // created, to briefly hint to the user that UI controls
        // are available.
        //delayedHide(100);
        hideToolbar();
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_game, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int id = item.getItemId();
        switch (id)
        {
            // Save State
            case R.id.save_state: {
                Toast.makeText(GameActivity.this, "Saving state", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Load State
            case R.id.load_state: {
                Toast.makeText(GameActivity.this, "Loading state", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Select Save State
            case R.id.select_save_state: {
                Toast.makeText(GameActivity.this, "Selecting save state", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Enable Sound
            case R.id.enable_sound: {
                boolean soundEnabled = !item.isChecked();
                Toast.makeText(GameActivity.this, soundEnabled ? "Sound Enabled" : "Sound Disabled", Toast.LENGTH_SHORT).show();
                item.setChecked(soundEnabled);
                return true;
            }

            // Change speed
            case R.id.change_speed: {
                Toast.makeText(GameActivity.this, "Changing speed", Toast.LENGTH_SHORT).show();
                return true;
            }

            // Settings
            case R.id.settings: {
                startActivity(new Intent(this, SettingsActivity.class));
                return true;
            }

            // Exit
            case R.id.exit: {
                finish();
                return true;
            }
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onBackPressed() {
        // showToolbar menu if not visible, otherwise exit out
        if (!mToolbarVisible)
            showToolbar();
        else
            finish();
    }

    private void hideToolbar() {
        // Hide UI first
        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.hide();
        }
        mToolbarVisible = false;

        // Note that some of these constants are new as of API 16 (Jelly Bean)
        // and API 19 (KitKat). It is safe to use them, as they are inlined
        // at compile-time and do nothing on earlier devices.
        mContentView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
    }

    private void showToolbar() {
        // Show the system bar
        mContentView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);

        mToolbarVisible = true;

        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.show();
        }
    }
}