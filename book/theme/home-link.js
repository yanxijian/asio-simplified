// Book home button: make the menu-bar title link back to the book landing
// page (which hosts the four-language switcher).
(function () {
  var title = document.querySelector('.menu-title');
  if (!title) return;
  var a = document.createElement('a');
  a.href = 'https://yanxijian.github.io/asio-simplified/';
  a.textContent = title.textContent;
  a.style.color = 'inherit';
  a.style.textDecoration = 'none';
  a.title = 'Book home';
  title.textContent = '';
  title.appendChild(a);
})();
