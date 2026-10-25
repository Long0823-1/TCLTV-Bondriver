package io.github.long0823.tclbondriver;
import android.content.*;
public final class NetworkBootReceiver extends BroadcastReceiver {
 @Override public void onReceive(Context c,Intent intent){if(c.getSharedPreferences(NetworkService.PREFS,0).getBoolean("enabled",false))c.startForegroundService(new Intent(c,NetworkService.class));}
}