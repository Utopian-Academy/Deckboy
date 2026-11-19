package io.github.utopian_academy.deckboy.pocket;

import org.libsdl.app.SDLActivity;

/** Deckboy Pocket: SDL's activity, loading Pocket's native libraries. */
public class PocketActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }
}
