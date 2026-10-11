package edu.ucla.nesl.sensordemo;

import android.content.Intent;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.media.MediaPlayer;
import android.os.Bundle;
import android.support.design.widget.FloatingActionButton;
import android.support.design.widget.Snackbar;
import android.support.v7.app.AppCompatActivity;
import android.support.v7.widget.Toolbar;
import android.view.View;
import android.widget.Button;
import android.widget.TextView;

import org.w3c.dom.Text;

public class SecondActivity extends AppCompatActivity implements SensorEventListener {

    private TextView textView2;

    private TextView txtX;
    private TextView txtY;
    private TextView txtZ;

    private Button btnWatch;
    private SensorManager sensorManager;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_second);
        Toolbar toolbar = (Toolbar) findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        textView2 = (TextView) findViewById(R.id.text_welcome);

        Intent intent = getIntent();
        String name_message = intent.getStringExtra(MainActivity.EXTRA_USERNAME);
        textView2.setText(String.format("Welcome %s", name_message));

        sensorManager = (SensorManager) getSystemService(SENSOR_SERVICE);

        final Sensor acc_sensor = sensorManager.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);

        txtX     = (TextView) findViewById(R.id.txt_x);
        txtY = (TextView) findViewById(R.id.txt_y);
        txtZ = (TextView) findViewById(R.id.txt_z);

        btnWatch = (Button) findViewById(R.id.buttonStart);
        btnWatch.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                sensorManager.registerListener(SecondActivity.this, acc_sensor, SensorManager.SENSOR_DELAY_NORMAL);
            }
        });

    }

    @Override
    public void onSensorChanged(SensorEvent sensorEvent) {
        txtX.setText(Float.toString(sensorEvent.values[0]));
        txtY.setText(Float.toString(sensorEvent.values[1]));
        txtZ.setText(Float.toString(sensorEvent.values[2]));

        double acc_norm = Math.sqrt(sensorEvent.values[0]*sensorEvent.values[0]+
                sensorEvent.values[1]*sensorEvent.values[1]+
                sensorEvent.values[2]*sensorEvent.values[2]);
        if (acc_norm > 15) {
            MediaPlayer mp = MediaPlayer.create(SecondActivity.this, R.raw.crash);
            mp.start();
        }


    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int i) {

    }
}
