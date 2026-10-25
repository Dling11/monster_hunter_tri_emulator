package com.oceantri.mh3;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;

/** Runs briefly in a separate process so native SDL state starts fresh. */
public final class RowellRestartActivity extends Activity {
    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        int previousPid = getIntent().getIntExtra("game_pid", -1);
        if (previousPid > 0 && previousPid != Process.myPid()) {
            Process.killProcess(previousPid);
        }
        Intent game = new Intent(this, OceanTriActivity.class);
        game.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
        startActivity(game);
        finish();
        Process.killProcess(Process.myPid());
    }
}
