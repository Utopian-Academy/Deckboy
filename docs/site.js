// Screenshot selection is progressive enhancement; the first image is usable without JS.
const deskImage = document.getElementById('desk-image');
const deskLink = document.getElementById('desk-full');
const themes = {'gameboy': 'Gameboy', 'dark': 'Dark', 'virtual-boy': 'Virtual Boy'};
document.querySelectorAll('[data-theme]').forEach(button => {
  button.addEventListener('click', () => {
    const theme = button.dataset.theme;
    if (!deskImage || !deskLink || !Object.hasOwn(themes, theme)) return;
    const src = `images/readme/release-419/example-02-${theme}.png`;
    deskImage.src = src;
    deskLink.href = src;
    deskImage.alt = `Deckboy 0.99.419 in the ${themes[theme]} theme, running the layers and titles example with three playlists feeding one programme.`;
    document.querySelectorAll('[data-theme]').forEach(choice => choice.setAttribute('aria-pressed', String(choice === button)));
  });
});

// Playing one demonstration pauses the others. Playback always starts with the visitor.
document.querySelectorAll('video').forEach(video => {
  video.addEventListener('play', () => {
    document.querySelectorAll('video').forEach(other => { if (other !== video) other.pause(); });
  });
});
