package tech.smallwonder.mp3fy;

import android.graphics.Bitmap;

import java.util.HashMap;

public class AudioFileInfo {
    /**
     * The album art related to this audio file. This can be null if this audio file does not have any associated album art
     */
    public Bitmap albumArt;

    /**
     * The list of metadata associated with this media file. Sometimes empty, never null
     */
    public HashMap<String, String> metadataList = new HashMap<>();
    public int bitrate = 0;

    /**
     * The duration of the audio file. This is in microseconds, so if you want to convert this to milliseconds, you divide the value by 1000, to convert to seconds, divide the value by 1000000
     */
    public long duration = 0;

    public AudioFileInfo() {}

    /**
     * List of possible metadata values that can be retrieved from this library.
     * This is just a list of convenient keys from the ones I can remember. Query the HashMap for every possible metadata key and values you can find, as media files can contain non-standard metadata keys.
     */
    public static class MetadataKeys {
        public static final String KEY_TITLE = "title";
        public static final String KEY_LYRICS = "lyrics";
        public static final String KEY_ALBUM = "album";
        public static final String KEY_ARTIST = "artist";
        public static final String KEY_GENRE = "genre";
        public static final String TRACK = "track";
        public static final String DATE = "date";
        public static final String ALBUM_ARTIST = "album_artist";
        public static final String COMPOSER = "composer";
        public static final String COMMENT = "comment";
    }

}
