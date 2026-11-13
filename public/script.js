// Runs only if the server sent this file with a JavaScript MIME type
const status = document.getElementById("js-status");
status.textContent = "loaded from /script.js as text/javascript";
status.className = "ok";
