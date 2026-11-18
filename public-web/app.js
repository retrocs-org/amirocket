const screenshot = document.getElementById('chat-screenshot');
const fullSize = document.getElementById('chat-screenshot-link');
for (const button of document.querySelectorAll('[data-screenshot]')) {
  button.addEventListener('click', () => {
    const theme = button.dataset.screenshot;
    screenshot.src = `assets/screenshots/chat-${theme}.png`;
    fullSize.href = screenshot.src;
    screenshot.alt = `AmiRocket running on AmigaOS 3.2 in the ${theme === 'mocha' ? 'Catppuccin Mocha' : 'Nord'} theme, with a multi-server sidebar, sample conversation and code block`;
    for (const choice of document.querySelectorAll('[data-screenshot]')) choice.setAttribute('aria-pressed', String(choice === button));
  });
}
