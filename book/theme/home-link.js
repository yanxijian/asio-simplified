// Book home button (QA-en follow-up): the menu-bar title links back to the
// book landing page, and a dedicated house-icon button sits next to the
// print button in the top bar — prominent on desktop and mobile.
(function () {
  var HOME = 'https://yanxijian.github.io/asio-simplified/';

  // 1. menu-bar title becomes a link
  var title = document.querySelector('.menu-title');
  if (title && !title.querySelector('a')) {
    var ta = document.createElement('a');
    ta.href = HOME;
    ta.textContent = title.textContent;
    ta.style.color = 'inherit';
    ta.style.textDecoration = 'none';
    ta.title = 'Book home';
    title.textContent = '';
    title.appendChild(ta);
  }

  // 2. dedicated house-icon button in the right-buttons group
  var buttons = document.querySelector('#mdbook-menu-bar .right-buttons')
    || document.querySelector('.right-buttons');
  if (!buttons || document.getElementById('mdbook-home-button')) return;

  var a = document.createElement('a');
  a.href = HOME;
  a.id = 'mdbook-home-button';
  a.className = 'icon-button';
  a.title = 'Book home (all languages)';
  a.setAttribute('aria-label', 'Book home (all languages)');
  a.innerHTML = '<span class="fa-svg">'
    + '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 576 512" '
    + 'aria-hidden="true"><path d="M575.8 255.5c0 18-15 32.1-32 32.1h-32l.7 '
    + '160.2c0 2.7-.2 5.4-.5 8.1V472c0 22.1-17.9 40-40 40H456c-4.4 0-8-3.6-8-8v-88c0-17.7-14.3-32-32-32h-64c-17.7 '
    + '0-32 14.3-32 32v88c0 4.4-3.6 8-8 8h-55.7c-22.1 0-40-17.9-40-40v-8.1c-.3-2.7-.5-5.4-.5-8.1l.7-160.2h-32c-17 '
    + '0-32-14.1-32-32.1 0-9 3-17 10-24L266.4 8c7-7 15-8 22-8s15 2 21 7L564.8 '
    + '231.5c8 7 12 15 11 24z"/></svg></span>';
  buttons.insertBefore(a, buttons.firstChild);
})();
