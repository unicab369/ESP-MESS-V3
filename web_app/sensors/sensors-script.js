// Global variables
let sensorChart = null;
let liveDataInterval = null;
let updateInterval = 1000; // 1 second
let dataPoints = [];
let maxPoints = 100;

// Initialize charts
function initCharts() {
	// Chart 1: Real-time sensor data
	const sensorData = {
		data: [[], []], // [timestamps, values]
		series: [
			{ label: "Time" },
			{ 
				label: "Sensor Value",
				stroke: "blue",
				width: 2,
				fill: "rgba(0, 119, 255, 0.1)"
			}
		]
	};
	
	const sensorOpts = {
		title: "Live Sensor Data",
		width: document.getElementById('sensorChart').offsetWidth,
		height: 280,
		scales: {
			x: { time: true },
			y: { 
				range: [0, 100],
				grid: { stroke: "#eee" }
			}
		},
		series: [
			{},
			{
				label: "Value",
				stroke: "#0077ff",
				width: 2
			}
		],
		axes: [
			{
				stroke: "#333",
				grid: { stroke: "#eee" }
			},
			{
				stroke: "#333",
				grid: { stroke: "#eee" }
			}
		]
	};
	
	sensorChart = new uPlot(sensorOpts, sensorData.data, document.getElementById('sensorChart'));
}

// Connect to ESP32 server
async function connectToServer() {
	const serverIp = document.getElementById('serverIp').value.trim();
	if (!serverIp) {
		alert('Please enter ESP32 IP address');
		return;
	}
	
	try {
		// Test connection
		const response = await fetch(`http://${serverIp}/info`, { method: 'GET' });
		if (response.ok) {
			const data = await response.json();
			document.getElementById('espIp').textContent = data.ip_address;
			document.getElementById('freeMemory').textContent = data.heap_free + ' bytes';
			console.log('Connected to:', data);
			
			// Start live data
			startLiveData();
		} else {
			throw new Error(`HTTP ${response.status}`);
		}
	} catch (error) {
		alert(`Failed to connect: ${error.message}`);
		console.error('Connection error:', error);
	}
}

async function reloadData() {
	const serverIp = document.getElementById('serverIp').value.trim();
	if (!serverIp) {
		alert('Please enter ESP32 IP address');
		return;
	}

	// Example argument
	const params = new URLSearchParams({
		device: 'aabbcc',
		date: '20251228',
	});

	try {
		const response = await fetch(`http://${serverIp}/data?${params.toString()}`, {
			method: 'GET',
		});

		if (response.ok) {
			const text = await response.text();
			console.log('Response preview:', text.substring(0, 500));
		} else {
			throw new Error(`HTTP ${response.status}`);
		}
	} catch (error) {
		alert(`Failed to connect: ${error.message}`);
		console.error('Connection error:', error);
	}
}

// Fetch live data from ESP32
async function fetchLiveData() {
	const serverIp = document.getElementById('serverIp').value.trim();
	if (!serverIp) return;
	
	try {
		// In a real app, you'd have an endpoint like /sensor-data
		// For now, we'll simulate data
		const now = Date.now() / 1000; // Current time in seconds
		const value = 50 + 30 * Math.sin(now * 0.1) + 10 * Math.random();
		
		// Add to data points
		dataPoints.push({
			timestamp: now,
			value: value
		});
		
		// Limit data points
		if (dataPoints.length > maxPoints) {
			dataPoints.shift();
		}
		
		// Update charts
		updateCharts();
		
		// Update UI
		document.getElementById('freeMemory').textContent = Math.floor(value * 1000) + ' bytes';
		
		// Update live data feed
		const liveDataDiv = document.getElementById('liveData');
		const timeStr = new Date(now * 1000).toLocaleTimeString('en-US', {
			hour12: true,
			hour: '2-digit',
			minute: '2-digit',
			second: '2-digit'
		});
		liveDataDiv.textContent = `Time: ${timeStr}\nValue: ${value.toFixed(2)}\n\n` + liveDataDiv.textContent;
		
		// Keep only last 10 lines
		const lines = liveDataDiv.textContent.split('\n').slice(0, 10);
		liveDataDiv.textContent = lines.join('\n');
		
	} catch (error) {
		console.error('Fetch error:', error);
	}
}

// Update both charts
function updateCharts() {
	if (dataPoints.length === 0) return;
	
	// Prepare data for sensor chart
	const timestamps = dataPoints.map(p => p.timestamp);
	const values = dataPoints.map(p => p.value);
	
	// Update sensor chart
	sensorChart.setData([timestamps, values]);
}

// Start live data updates
function startLiveData() {
	if (liveDataInterval) {
		clearInterval(liveDataInterval);
	}
	
	liveDataInterval = setInterval(fetchLiveData, updateInterval);
	console.log('Live data started, interval:', updateInterval, 'ms');
}

// Stop live data
function stopLiveData() {
	if (liveDataInterval) {
		clearInterval(liveDataInterval);
		liveDataInterval = null;
		console.log('Live data stopped');
	}
}

// Update refresh rate
function updateRate(seconds) {
	updateInterval = seconds * 1000;
	document.getElementById('updateRate').textContent = seconds;
	
	if (liveDataInterval) {
		startLiveData(); // Restart with new interval
	}
}

// Clear all charts
function clearCharts() {
	dataPoints = [];
	updateCharts();
	document.getElementById('liveData').textContent = 'Data cleared';
}