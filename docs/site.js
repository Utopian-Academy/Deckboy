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

// Silent demos behave like GIFs: they play while on screen and pause when
// scrolled away, with no controls. Visitors who ask for reduced motion keep
// the controls and press play themselves.
const loops = document.querySelectorAll('video[data-loop-demo]');
const stillPlease = window.matchMedia('(prefers-reduced-motion: reduce)').matches;
if (!stillPlease && 'IntersectionObserver' in window) {
  const onScreen = new IntersectionObserver(entries => entries.forEach(entry => {
    if (entry.isIntersecting) entry.target.play().catch(() => {});
    else entry.target.pause();
  }), {threshold: 0.25});
  loops.forEach(video => {
    video.controls = false;
    video.muted = true;
    onScreen.observe(video);
  });
}

// Videos with sound start with the visitor, and playing one pauses the others.
const withSound = document.querySelectorAll('video:not([data-loop-demo])');
withSound.forEach(video => {
  video.addEventListener('play', () => {
    withSound.forEach(other => { if (other !== video) other.pause(); });
  });
});
