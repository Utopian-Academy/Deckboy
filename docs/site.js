// Screenshot selection is progressive enhancement; the first image is usable without JS.
const deskImage = document.getElementById('desk-image');
const deskLink = document.getElementById('desk-full');
const themes = {'gameboy': 'Gameboy', 'dark': 'Dark', 'virtual-boy': 'Virtual Boy'};
document.querySelectorAll('[data-theme]').forEach(button => {
  button.addEventListener('click', () => {
    const theme = button.dataset.theme;
    if (!deskImage || !deskLink || !Object.hasOwn(themes, theme)) return;
    const src = `images/readme/desk-${theme}.png`;
    deskImage.src = src;
    deskLink.href = src;
    deskImage.alt = `Deckboy 0.99.417 in the ${themes[theme]} theme, with built-in patterns on the cue list, Swirl on the programme monitor and its inspector.`;
    document.querySelectorAll('[data-theme]').forEach(choice => choice.setAttribute('aria-pressed', String(choice === button)));
  });
});

// Playing one demonstration pauses the others. Playback always starts with the visitor.
document.querySelectorAll('video').forEach(video => {
  video.addEventListener('play', () => {
    document.querySelectorAll('video').forEach(other => { if (other !== video) other.pause(); });
  });
});
