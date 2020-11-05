package tech.smallwonder.mp3fy;

import android.graphics.Bitmap;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.util.HashMap;

import tech.smallwonder.mp3fy.interfaces.OnFailureListener;
import tech.smallwonder.mp3fy.interfaces.OnMetadataAvailableListener;
import tech.smallwonder.mp3fy.interfaces.OnSuccessListener;

public class MP3fy {

    private long media_handle;

    private static MP3fy instance = new MP3fy();

    static {
        System.loadLibrary("avcodec");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("mp3fy");
    }

    private MP3fy() {
        if (BuildConfig.DEBUG) {
            new Thread(new Runnable() {
                @Override
                public void run() {
                    pipeStdErrToLogcatNative();
                }
            }).start();
        }
    }

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
        media_handle = initializeNative(fileToConvert, outputFile);
        return media_handle != -1;
    }

    /**
     * Make sure you have called initialize() before calling this method
     * Starts the conversion. Note that this call will block the calling thread until the operation is completed. If you want asynchronous conversion, use the convertAsync method instead.
     * @return true if the conversion operation was successful and false otherwise
     */
    public boolean convert() {
        return convertNative(media_handle);
    }

    /**
     * Make sure you have called initialize() before calling this method
     * Start the conversion on a new thread and calls the appropriate listener when the operation is complete
     * @param listener - The success listener
     * @param listener2 - The failure/error listener
     */
    public void convertAsync(final OnSuccessListener listener, final OnFailureListener listener2) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                boolean success = convertNative(media_handle);
                if (success) {
                    listener.onSuccess();
                } else {
                    listener2.onFailure();
                }
            }
        }).start();
    }

    /**
     * Returns the current progress of the conversion process. This can and should only be used in async mode of the media conversion function. Any other scenario might cause this code to crash. You've been warned!
     * @return the current conversion progress, -1 on error.
     */
    public int getPercentage() {
        if (media_handle == -1) return -1;
        return getPercentageNative(media_handle);
    }

    /**
     * Fetches all the metadata available in this media file
     * This method might take some time to complete, so it's probably better to call this in a background thread
     * @see MP3fy#getAllMetadataAsync(String, OnMetadataAvailableListener)
     * @see MP3fy#getAllMetadata(File)
     * @param path - The path to the file we want to fetch the metadata
     * @return - ArrayList of HashMap<String, String> containing the metadata or an empty HashMap on error
     */
    public HashMap<String, String> getAllMetadata(String path) {
        return getAllMetadataNative(path);
    }

    /**
     * Like getAllMetadata(String), but asynchronous.
     */
    public void getAllMetadataAsync(final String path, final OnMetadataAvailableListener metadataAvailableListener) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                HashMap<String, String> metadata = getAllMetadataNative(path);
                metadataAvailableListener.onMetadataAvailable(metadata);
            }
        }).start();
    }

    /**
     * Fetches all the metadata available in this media file
     * This method might take some time to complete, so it's probably better to call this in a background thread
     * @param file - The file to get metadata for
     * @return - ArrayList of HashMap<String, String> containing the metadata or an empty HashMap on error
     */
    public HashMap<String, String> getAllMetadata(File file) {
        return getAllMetadata(file.getAbsolutePath());
    }

    /**
     * Fetches the album art associated with the specified image file (if there's one)
     * This method might take some time to complete, so it's probably better to call this in a background thread
     * @param path - Path to the audio file to fetch album art for
     * @return the album art bitmap if there's one or null otherwise
     */
    public Bitmap getAlbumArt(String path) {
        return getAlbumArtNative(path);
    }

    /**
     * Fetches the album art associated with the specified image file (if there's one)
     * This method might take some time to complete, so it's probably better to call this in a background thread
     * @param file - File to the audio file to fetch album art for
     * @return the album art bitmap if there's one or null otherwise
     */
    public Bitmap getAlbumArt(File file) {
        return getAlbumArt(file.getAbsolutePath());
    }

    /**
     * Gets information about the audio file, including the album art.
     * This method might take some time to complete, so it's probably better to call this in a background thread
     * @param path - Path to the audio file to get the info
     * @return the audio file information or null on error.
     */
    public AudioFileInfo getAudioFileInfo(String path) {
        return getAudioFileInfoNative(path);
    }

    /**
     * Gets information about the audio file, including the album art.
     * This method might take some time to complete, so it's probably better to call this in a background thread.
     * @param file - Path to the audio file to get the info
     * @return the audio file information or null on error
     */
    public AudioFileInfo getAudioFileInfo(File file) {
        return getAudioFileInfo(file.getAbsolutePath());
    }

    /**
     * Edit metadata info stored in inputFile and store the result in outputFile, with the album art
     * Note that not all metadata will be set if the audio file format does not allow it
     * @param inputFile Audio file to set metadata
     * @param metadataInfos Metadata information to be set
     * @param newBitmap Bitmap to use as the album art
     * @param outputFile Output file with the new metadata information
     * @return true if the operation completed successfully, false otherwise
     */
    public boolean editMetadataInformation(String inputFile, HashMap<String, String> metadataInfos, Bitmap newBitmap, String outputFile) {
        ByteArrayOutputStream byteArrayOutputStream = new ByteArrayOutputStream();
        newBitmap.compress(Bitmap.CompressFormat.JPEG, 100, byteArrayOutputStream);

        byte[] data = byteArrayOutputStream.toByteArray();

        String[] keySet = new String[1];
        keySet = metadataInfos.keySet().toArray(keySet);
        String[] valueSet = new String[1];
        valueSet = metadataInfos.values().toArray(valueSet);

        return editMetadataInformationNative(inputFile, keySet, valueSet, metadataInfos.size(), data, data.length, newBitmap.getWidth(), newBitmap.getHeight(), outputFile);
    }

    /**
     * Edit metadata info stored in inputFile and store the result in outputFile
     * Note that not all metadata will be set if the audio file format does not allow it.
     * It is the caller's responsibility to ascertain that all the information set is reflected in the new file
     * @param inputFile Audio file to set metadata
     * @param metadataInfos Metadata information to be set
     * @param outputFile Output file with the new metadata information
     * @return true if the operation completed successfully, false otherwise
     */
    public boolean editMetadataInformation(String inputFile, HashMap<String, String> metadataInfos, String outputFile) {
        String[] keySet = new String[1];
        keySet = metadataInfos.keySet().toArray(keySet);
        String[] valueSet = new String[1];
        valueSet = metadataInfos.values().toArray(valueSet);
        return editMetadataInformationNative(inputFile, keySet, valueSet, metadataInfos.size(), null, 0, 0, 0, outputFile);
    }

    /////////////////////////////////////////////////////////////////////////////////

    //                             NATIVE METHODS GO HERE                          //

    //////////////////////////////////////////////////////////////////////////////////

    /**
     * Initializes the input and output file and prepares for conversion
     * Please make sure the input and output file paths are valid before passing to this function
     *
     * @return The pointer handle to the media file
     */
    private native long initializeNative(String inputFile, String outputFile);

    /**
     * Starts the audio conversion and writes the data to the audio file specified in @initialize.
     * @return true if the whole operation was successful and false otherwise
     */
    private native boolean convertNative(long media_id);

    /**
     * Fetches all the metadata available in this media file
     * @param path - The path to the file we want to fetch the metadata
     * @return - ArrayList of HashMap<String, String> containing the metadata or an empty HashMap on error
     */
    private native HashMap<String, String> getAllMetadataNative(String path);

    /**
     * Returns the associated cover image from this audio file (if any)
     * @param path A valid path to an audio file.
     * @return the bitmap associated with this audio file. Returns null if there is no
     */
    private native Bitmap getAlbumArtNative(String path);

    /**
     * Returns the information about this audio file in the AudioFileInfo class.
     * Members can be queried for information.
     * @param path - A valid path to an audio file.
     * @return the AudioFileInfo or null on error.
     */
    private native AudioFileInfo getAudioFileInfoNative(String path);

    /**
     * Returns the current progress of the conversion process (in percentage)
     * This can and should only be used when converting in asynchronous mode and also have to make sure that there is a valid conversion in progress before calling this method. Any other circumstance will cause undefined behavior :)
     * @param media_id - The pointer (handle) to the media instance we're currently manipulating
     * @return the percentage of the conversion process on success, and -1 on error. It might even crash the entire application if not used appropriately, so use this carefully.
     */
    private native int getPercentageNative(long media_id);

    private native boolean editMetadataInformationNative(String inputFile, String[] keys, String[] values, int length, byte[] albumArt, int albumArtLen, int width, int height, String outputFile);

    private native void pipeStdErrToLogcatNative();

}
