package tech.smallwonder.mp3fy.interfaces;

import java.util.HashMap;

public interface OnMetadataAvailableListener {
    void onMetadataAvailable(HashMap<String, String> metadata);
}
