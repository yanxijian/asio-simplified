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
    + '<svg xmlns="http://www.w3.org/2000/svg" viewBox="2 2 20 20" '
    + 'style="fill:none;stroke:currentColor;stroke-width:2;'
    + 'stroke-linecap:round;stroke-linejoin:round" aria-hidden="true">'
    + '<path d="M3 10.5 12 3l9 7.5"/>'
    + '<path d="M5 9.5V21h5v-6h4v6h5V9.5"/></svg></span>';
  buttons.insertBefore(a, buttons.firstChild);
})();
