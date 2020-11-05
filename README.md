MP3fy
======
This library helps to convert video files to mp3. It also allows you to edit metadata on audio files and also change or add album arts for the files.

The library uses FFMPEG's C libraries under the hood, and bundles all the required dependencies so you don't have to fetch any other dependencies.

It can also fetch info (metadata) about audio files, and can even extract and give you the album art associated with the audio (if available)

#Usage
=======
This is a fairly simple library. To convert a video file to mp3, you can do:
```java
// We can start conversion. All internals are 
if (MP3fy.getInstance().initialize(inputFilePath, outputFilePath) {
    if (MP3fy.convert()) {
        // Conversion is successful!
    } else {
        // Could not convert this file for some reason...
    }
}
```

Or if you want to do the conversion asynchronously,
```java
if (MP3fy.getInstance().initialize(inputFilePath, outputFilePath) {
    MP3fy.convertAsync(successListener, failureListener);
}
```

To fetch metadata for audio file (without album art)
```java
HashMap<String, String> metadata = MP3fy.getInstance().getAllMetadata(path);
```
This returns all the metadata if they can be found inside the file, or an empty HashMap if not.
The keys can be found in the AudioFileInfo.MetadataKeys class. Note that this list is not comprehensive, and does not contain all possible keys contained in the hash map. It's your job to figure out other data present in the list and deal with them accordingly. There is also an asynchronous version of this method.

To fetch metadata for audio file (with album art)
```java
AudioFileInfo fileInfo = MP3fy.getInstance().getAudioFileInfo(path);
``` 

