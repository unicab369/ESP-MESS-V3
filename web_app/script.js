// Update uptime every second
let uptime = 0;
setInterval(() => {
    uptime++;
    document.getElementById('uptime').textContent = uptime;
}, 1000);

// Fetch system info on page load
fetch('/info')
    .then(response => response.json())
    .then(data => {
        document.getElementById('freeHeap').textContent = data.heap_free;
        document.getElementById('ipAddress').textContent = data.ip_address;
    });