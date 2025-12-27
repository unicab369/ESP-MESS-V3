// Update uptime every second
let uptime = 0;
setInterval(() => {
    uptime++;
    // document.getElementById('uptime').textContent = uptime;
}, 1000);

let savedServers = ['192.168.1.100', '192.168.1.101', '192.168.1.102'];

function showStatus(message, type = 'info') {
	const statusDiv = document.getElementById('status');
	statusDiv.textContent = message;
	statusDiv.className = `status ${type}`;
	statusDiv.classList.remove('hidden');
	
	if (type === 'success') {
		setTimeout(() => statusDiv.classList.add('hidden'), 3000);
	}
}

function useSavedServer(ip) {
	document.getElementById('serverIp').value = ip;
	showStatus(`Selected server: ${ip}`, 'success');
}

async function fetchServerInfo() {
	const serverIp = document.getElementById('serverIp').value.trim();
	
	if (!serverIp) {
		showStatus('Please enter a server IP address', 'error');
		return;
	}
	
	// Validate IP format (basic validation)
	const ipPattern = /^(\d{1,3}\.){3}\d{1,3}$/;
	if (!ipPattern.test(serverIp)) {
		showStatus('Invalid IP address format', 'error');
		return;
	}
	
	showStatus('Fetching server information...', 'loading');
	
	const startTime = Date.now();
	
	try {
		const response = await fetch(`http://${serverIp}/info`, {
			method: 'GET',
			headers: {
				'Accept': 'application/json',
				'Content-Type': 'application/json'
			},
			timeout: 5000  // 5 second timeout
		});
		
		const responseTime = Date.now() - startTime;
		
		if (!response.ok) {
			throw new Error(`HTTP error! status: ${response.status}`);
		}
		
		const data = await response.json();
		
		// Update UI with fetched data
		document.getElementById('displayIp').textContent = data.ip_address || serverIp;
		document.getElementById('heapMemory').textContent = 
			data.heap_free ? `${data.heap_free.toLocaleString()} bytes` : '-';
		document.getElementById('connectionStatus').textContent = 'Connected ✓';
		document.getElementById('connectionStatus').style.color = '#28a745';
		document.getElementById('responseTime').textContent = `${responseTime}ms`;
		
		// Add to saved servers if not already there
		if (!savedServers.includes(serverIp)) {
			savedServers.push(serverIp);
			updateServerList();
		}
		
		showStatus(`Successfully fetched data from ${serverIp} in ${responseTime}ms`, 'success');
		
	} catch (error) {
		console.error('Error fetching server info:', error);
		
		// Update UI with error state
		document.getElementById('displayIp').textContent = serverIp;
		document.getElementById('heapMemory').textContent = '-';
		document.getElementById('connectionStatus').textContent = 'Failed to connect ✗';
		document.getElementById('connectionStatus').style.color = '#dc3545';
		document.getElementById('responseTime').textContent = '-';
		
		showStatus(`Failed to connect to ${serverIp}: ${error.message}`, 'error');
	}
}

function updateServerList() {
	const serverListDiv = document.getElementById('serverList');
	serverListDiv.innerHTML = '<h3>📋 Saved Servers</h3>';
	
	savedServers.forEach(ip => {
		const serverItem = document.createElement('div');
		serverItem.className = 'server-item';
		serverItem.onclick = () => useSavedServer(ip);
		serverItem.innerHTML = `
			<span class="server-ip">${ip}</span> - Click to select
		`;
		serverListDiv.appendChild(serverItem);
	});
}