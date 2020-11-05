package tech.smallwonder.testmp3fy;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.os.Build;
import android.os.Bundle;
import android.os.CountDownTimer;
import android.text.format.DateUtils;
import android.util.Log;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;

import tech.smallwonder.mp3fy.AudioFileInfo;
import tech.smallwonder.mp3fy.MP3fy;

public class MainActivity extends AppCompatActivity {

    private EditText editText;
    private EditText editText2;
    private EditText pathBox;
    private EditText titleBox;
    private EditText artistBox;
    private EditText albumBox;
    private EditText albumArtBox;
    private ImageView albumArt;
    private TextView title;
    private TextView artist;
    private TextView album;
    private TextView lyrics;
    private TextView duration;
    private TextView conversionStatus;
    private Button start;
    private Button convert;
    private Button useFile;
    private Button addMetadata;

    boolean pendingJob = false;
    File finishedFile;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE}, 100);
            }
        }

        editText = findViewById(R.id.editText);
        editText2 = findViewById(R.id.editText2);
        pathBox = findViewById(R.id.path_box);
        titleBox = findViewById(R.id.title_box);
        artistBox = findViewById(R.id.artist_box);
        albumBox = findViewById(R.id.album_box);
        albumArtBox = findViewById(R.id.album_art_box);
        albumArt = findViewById(R.id.song_album_art);
        title = findViewById(R.id.song_title);
        artist = findViewById(R.id.song_artist);
        album = findViewById(R.id.song_album);
        lyrics = findViewById(R.id.song_lyrics);
        duration = findViewById(R.id.song_duration);
        conversionStatus = findViewById(R.id.conversion_status);
        start = findViewById(R.id.btn);
        convert = findViewById(R.id.convert);
        useFile = findViewById(R.id.use_file);
        addMetadata = findViewById(R.id.add_metadata);

        useFile.setOnClickListener((v) -> {
            Bitmap bitmap = BitmapFactory.decodeFile("/storage/emulated/0/DCIM/Camera/IMG_20200519_225904.jpg");
            if (bitmap == null) {
                Toast.makeText(this, "Could not make bitmap from this file!", Toast.LENGTH_SHORT).show();
            }

            String path = pathBox.getText().toString();
            if (!path.trim().isEmpty()) {
                File file = new File(path);
                if (file.exists()) {
                    // Get the file metadata
                    HashMap<String, String> metadatas = MP3fy.getInstance().getAllMetadata(file);
                    String title = metadatas.get(AudioFileInfo.MetadataKeys.KEY_TITLE);
                    if (title != null) {
                        titleBox.setText(title);
                    }
                    String artist = metadatas.get(AudioFileInfo.MetadataKeys.KEY_ARTIST);
                    if (artist != null) {
                        artistBox.setText(artist);
                    }
                    String album = metadatas.get(AudioFileInfo.MetadataKeys.KEY_ALBUM);
                    if (album != null) {
                        albumBox.setText(album);
                    }
                } else {
                    Toast.makeText(this, "Path specified does not exist", Toast.LENGTH_SHORT).show();
                }
            }
        });

        addMetadata.setOnClickListener((v) -> {
            String path = pathBox.getText().toString();
            if (!path.trim().isEmpty()) {
                File file = new File(path);
                if (file.exists()) {
                    HashMap<String, String> metadatas = new HashMap<>();
                    metadatas.put(AudioFileInfo.MetadataKeys.KEY_TITLE, titleBox.getText().toString());
                    metadatas.put(AudioFileInfo.MetadataKeys.KEY_ARTIST, artistBox.getText().toString());
                    metadatas.put(AudioFileInfo.MetadataKeys.KEY_ALBUM, albumBox.getText().toString());
                    String newFileName = "test".concat(file.getName().substring(file.getName().lastIndexOf(".")));
                    Bitmap bitmap = BitmapFactory.decodeFile("/storage/emulated/0/DCIM/Camera/IMG_20200519_225904.jpg");
                    if (bitmap == null) {
                        Toast.makeText(this, "Could not make bitmap from this file!", Toast.LENGTH_SHORT).show();
                    }

                    if (MP3fy.getInstance().editMetadataInformation(file.getAbsolutePath(), metadatas, bitmap, new File(getFilesDir(), newFileName).getAbsolutePath())) {
                        // Ask the user where they want to save the new file
                        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.KITKAT) {

                            finishedFile = new File(getFilesDir(), newFileName);

                            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                            intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                            intent.addCategory(Intent.CATEGORY_OPENABLE);
                            intent.setType("audio/*");
                            intent.putExtra(Intent.EXTRA_TITLE, file.getName());
                            if (intent.resolveActivity(getPackageManager()) != null)
                                startActivityForResult(intent, 200);
                        }
                    } else {
                        Toast.makeText(this, "Unable to edit file metadata for some weird reason", Toast.LENGTH_SHORT).show();
                    }
                } else {
                    Toast.makeText(this, "Path specified does not exist", Toast.LENGTH_SHORT).show();
                }
            }
        });

        start.setOnClickListener((v) -> {
            String text = editText.getText().toString();
            AudioFileInfo fileInfo = MP3fy.getInstance().getAudioFileInfo(text);
            if (fileInfo != null) {
                Log.i("MainActivity", "Metadata keys: " + Arrays.toString(MP3fy.getInstance().getAllMetadata(text).keySet().toArray()));
                String titleText = fileInfo.metadataList.get(AudioFileInfo.MetadataKeys.KEY_TITLE);
                String artistText = fileInfo.metadataList.get(AudioFileInfo.MetadataKeys.KEY_ARTIST);
                String albumText = fileInfo.metadataList.get(AudioFileInfo.MetadataKeys.KEY_ALBUM);
                String lyricsText = fileInfo.metadataList.get(AudioFileInfo.MetadataKeys.KEY_LYRICS);
                if (lyricsText == null) {
                    for (String key : fileInfo.metadataList.keySet()) {
                        if (key.contains("lyrics")) {
                            lyricsText = fileInfo.metadataList.get(key);
                            break;
                        }
                    }
                }
                long durationValue = fileInfo.duration;

                if (titleText != null) {
                    title.setText(titleText);
                }

                if (albumText != null) {
                    album.setText(albumText);
                }

                if (artistText != null) {
                    artist.setText(artistText);
                }

                if (lyricsText != null) {
                    lyrics.setText(lyricsText);
                }

                if (fileInfo.albumArt != null) {
                    albumArt.setImageBitmap(MP3fy.getInstance().getAlbumArt(new File(text)));
                } else {
                    Toast.makeText(this, "File album art is NULL", Toast.LENGTH_SHORT).show();
                }
                duration.setText(DateUtils.formatElapsedTime(durationValue / 1000000));
            } else {
                Toast.makeText(this, "File info is NULL", Toast.LENGTH_SHORT).show();
            }
        });

        convert.setOnClickListener((v) -> {
            String text = editText2.getText().toString();
            if (MP3fy.getInstance().initialize(text, new File(getFilesDir(), "test.mp3").getAbsolutePath())) {
                conversionStatus.setText("Converting...");

                // Tell me the percentage every 300 milliseconds. The timer is slated to run for 10 hours
                CountDownTimer timer = new CountDownTimer((1000 * 60 * 60 * 10), 300) {
                    @Override
                    public void onTick(long millisUntilFinished) {
                        int percentage = MP3fy.getInstance().getPercentage();
                        if (percentage != -1) {
                            conversionStatus.setText(String.format("%d%s", percentage, "%"));
                        }
                    }

                    @Override
                    public void onFinish() {

                    }
                };

                MP3fy.getInstance().convertAsync(() -> {
                    conversionStatus.setText("Done!");
                    timer.cancel();
                }, () -> {
                    conversionStatus.setText("Conversion failed!");
                    timer.cancel();
                });

                timer.start();

            } else {
                Toast.makeText(this, "Could not initialize converter!", Toast.LENGTH_SHORT).show();
            }

        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == 200 && resultCode == RESULT_OK) {
            try {
                if (data != null && data.getData() != null) {
                    BufferedOutputStream outputStream = new BufferedOutputStream(getContentResolver().openOutputStream(data.getData()));
                    BufferedInputStream inputStream = new BufferedInputStream(new FileInputStream(finishedFile));
                    byte[] bytes = new byte[1024];
                    int numRead;
                    while ((numRead = inputStream.read(bytes)) >= 0) {
                        outputStream.write(bytes, 0, numRead);
                    }
                    Toast.makeText(this, "Modified the original file!", Toast.LENGTH_SHORT).show();
                }
            } catch (IOException e) {
                e.printStackTrace();
                Toast.makeText(this, "An error occurred while saving file: " + e.getMessage(), Toast.LENGTH_SHORT).show();
            }
        }
    }
}
