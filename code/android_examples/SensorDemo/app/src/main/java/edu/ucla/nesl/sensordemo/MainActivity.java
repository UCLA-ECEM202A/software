package edu.ucla.nesl.sensordemo;

import android.content.Intent;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;

public class MainActivity extends AppCompatActivity {
    private Button btnContinue;
    private TextView txtMessage;
    private EditText et_name;
    public static final String EXTRA_USERNAME = "edu.ucla.nesl.android_demo.USER_NAME";


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        btnContinue = (Button) findViewById(R.id.btn_continue);
        et_name = (EditText) findViewById(R.id.editText);

        txtMessage = (TextView) findViewById(R.id.txt_welcome);
        btnContinue.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                txtMessage.setText("Will move to another screen");
                Intent intent = new Intent(MainActivity.this, SecondActivity.class);
                intent.putExtra(EXTRA_USERNAME, et_name.getText().toString());
                startActivity(intent);
            }
        });
    }
}
