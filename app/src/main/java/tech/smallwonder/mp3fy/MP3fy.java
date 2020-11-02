package tech.smallwonder.mp3fy;

import java.util.HashMap;

import tech.smallwonder.mp3fy.interfaces.OnFailureListener;
import tech.smallwonder.mp3fy.interfaces.OnSuccessListener;

public class MP3fy {

    private long media_handle;

    private static MP3fy instance = new MP3fy();

    static {
        System.loadLibrary("avcodec");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("mp3fy");
    }

    private MP3fy() {}

    public static MP3fy getInstance() {
        return instance;
    }

    /**
     * Initializes the converter with input and output information. This must be called for every media file you want to convert. Please make sure that the file path provided here is complete and absolute.
     * @param fileToConvert - The input file
     * @param outputFile - The expected output. This is the MP3 file
     * @return true if the operation succeeds and false otherwise
     */
    public boolean initialize(String fileToConvert, String outputFile) {
        media_handle = nativeInitialize(fileToConvert, outputFile);
        return media_handle != -1;
    }

    /**
     * Starts the conversion. Note that this call will block the calling thread until the operation is completed. If you want asynchronous conversion, use the convertAsync method instead.
     * @return true if the conversion operation was successful and false otherwise
     */
    public boolean convert() {
        return nativeConvert(media_handle);
    }

    /**
     * Start the conversion on a new thread and calls the appropriate listener when the operation is complete
     * @param listener - The success listener
     * @param listener2 - The failure/error listener
     */
    public void convertAsync(final OnSuccessListener listener, final OnFailureListener listener2) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                boolean success = nativeConvert(media_handle);
                if (success) {
                    listener.onSuccess();
                } else {
                    listener2.onFailure();
                }
            }
        }).start();
    }

    /**
     * Initializes the input and output file and prepares for conversion
     * Please make sure the input and output file paths are valid before passing to this function
     *
     * @return The pointer handle to the media file
     */
    private native long nativeInitialize(String inputFile, String outputFile);

    /**
     * Starts the audio conversion and writes the data to the audio file specified in @initialize.
     * @return true if the whole operation was successful and false otherwise
     */
    private native boolean nativeConvert(long media_id);

    /**
     * Fetches all the metadata available in this media file
     * @param path - The path to the file we want to fetch the metadata
     * @return - ArrayList of HashMap<String, String> containing the metadata or an empty HashMap on error
     */
    private native HashMap<String, String> getAllMetadata(String path);

}
