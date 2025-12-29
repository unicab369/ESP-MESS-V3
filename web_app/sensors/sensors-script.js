// Global variables
let sensorChart = null;
let sensorChart2 = null;
let liveDataInterval = null;
let updateInterval = 1000; // 1 second
let dataPoints = [];
let maxPoints = 100;
let indexDB = null;

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

	// Chart 2: Historical data
	const sensorData2 = {
		data: [[], [], [], []],
		series: [
			{ label: "Time" },
			{ 
				label: "Temp",
				stroke: "red",
				width: 1
			},
			{ 
				label: "Hum",
				stroke: "green",
				width: 1
			},
			{ 
				label: "Lux",
				stroke: "blue",
				width: 2
			}
		]
	};
	
	const sensorOpts2 = {
		width: document.getElementById('sensorChart2').offsetWidth,
		height: 280,

		// cursor: {
		// 	drag: {
		// 		x: true,
		// 		y: true,
		// 		setScale: false, // Default to panning
		// 	},
		// },

		scales: {
			x: { 
				time: true,
				auto: true  // This will auto-range based on your timestamps
			},
			y: { 
				range: [0, 100],  // For temp/humidity
				grid: { stroke: "#eee" }
			},
			y2: {
				side: 1,  // Right side for lux
				grid: { stroke: "#e0e0e0" }
			}
		},

		axes: [
			{
				scale: "x",
				grid: { stroke: "#eee" }
			},
			{
				scale: "y",
				values: (u, vals) => vals.map(v => v.toFixed(1)),
				grid: { stroke: "#eee" }
			},
			{
				scale: "y2",
				side: 1,
				values: (u, vals) => vals.map(v => v.toFixed(0)),
				grid: { stroke: "#e0e0e0" }
			}
		],
		series: [
			{
				// X-axis series
				scale: "x",
				value: (u, v) => {
					const date = new Date(v);
					return date.toLocaleString();
				}
			},
			{
				scale: "y",
				label: "Temp(°C)",
				stroke: "#ff4444",
				width: 1,
				value: (u, v) => v?.toFixed(1) + "°C"
			},
			{
				scale: "y",
				label: "Hum(%)",
				stroke: "#4444ff",
				width: 1,
				value: (u, v) => v?.toFixed(1) + "%"
			},
			{
				scale: "y2",
				label: "Lux",
				stroke: "#36454F",
				width: 1,
				value: (u, v) => v?.toFixed(0) + " lux"
			}
		]
	};
	
	sensorChart2 = new uPlot(sensorOpts2, sensorData2.data, document.getElementById('sensorChart2'));
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


async function reloadData(dateStr = null) {
	const serverIp = document.getElementById('serverIp').value.trim();
	if (!serverIp) {
		alert('Please enter ESP32 IP address');
		return;
	}

	let deviceId = 'aabbcc';

	// Example argument
	const params = new URLSearchParams({
		device: deviceId,
		date: dateStr || 'latest',
	});
	console.log('Fetching data:', params.toString());

	indexDB_setup(deviceId);

	try {
		let startTime = Date.now();

		const response = await fetch(`http://${serverIp}/data?${params.toString()}`, {
			method: 'GET',
		});

		if (response.ok) {
			// const text = await response.text();
			// console.log('Response:', text);

			const buffer = await response.arrayBuffer();
			const RECORD_SIZE = 10; // 4 + 2 + 2 + 2 = 10 bytes
			const recordCount = Math.floor(buffer.byteLength / RECORD_SIZE);
			const dataView = new DataView(buffer);
			console.log('Response time:', Date.now() - startTime, 'ms');

			const sensorData = [];
			const timeStampArr = [];
			const tempArr = [];
			const humArr = [];
			const luxArr = [];
			
			startTime = Date.now();

			for (let i = 0; i < recordCount; i++) {
				const timestamp = dataView.getUint32(i * RECORD_SIZE, true);		// 4 bytes
				const temperature = dataView.getUint16(i * RECORD_SIZE + 4, true);	// 2 bytes
				const humidity = dataView.getInt16(i * RECORD_SIZE + 6, true);		// 2 bytes
				const lux = dataView.getUint16(i * RECORD_SIZE + 8, true);			// 2 bytes
				
				timeStampArr.push(timestamp);
				tempArr.push(temperature);
				humArr.push(humidity);
				luxArr.push(lux);

				// sensorData.push({
				// 	timestamp: timestamp,
				// 	temp: temperature,
				// 	hum: humidity,
				// 	lux: lux
				// });

				indexDB_putReading(deviceId, timestamp, temperature, humidity, lux);
			}

			// console.log('Sensor data:', sensorData);
			console.log('count:', recordCount);
			console.log('Process time:', Date.now() - startTime, 'ms');
			sensorChart2.setData([timeStampArr, tempArr, humArr, luxArr]);
			timeWindow_apply();
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


//# %%%%%%%%%%%%%%%%%%%%%%%%%%% UPDATE WINDOW %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
let chartUpdateTimer = null;
let isChartPaused = false;

function updateWindow_pause() {
	clearInterval(chartUpdateTimer);
	chartUpdateTimer = null;
}

// Apply update interval
function updateWindow_apply() {
	const select = document.getElementById('updateWindow');
	const value = parseInt(select.value);

	// Clear existing timer
	if (chartUpdateTimer) {
		clearInterval(chartUpdateTimer);
		chartUpdateTimer = null;
	}
	
	// Start new timer if not "Manual only"
	if (value > 0) {
		// Update immediately once
		reloadData();
		
		// Set up interval
		chartUpdateTimer = setInterval(() => {
			reloadData();
		}, value);
	}
	
	console.log(`Chart update interval set to: ${value}ms`);
}


//# %%%%%%%%%%%%%%%%%%%%%%%%%%% TIME WINDOW %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

function timeWindow_apply() {
	const select = document.getElementById('timeWindow');
	const minutes = parseInt(select.value);
	if (!sensorChart2) return;
	
	const timestamps = sensorChart2.data[0];
	if (timestamps.length === 0) return;
	
	let xMin, xMax;
	
	if (minutes === 0) {
		// Show all data
		xMin = Math.min(...timestamps);
		xMax = Math.max(...timestamps);
	} else {
		// Show last X minutes
		const latestTime = Math.max(...timestamps);
		xMax = latestTime;
		xMin = latestTime - (minutes * 60);
	}
	
	// Apply zoom
	sensorChart2.setScale('x', { min: xMin, max: xMax });
}

// Reset to show all data
function timeWindow_reset() {
	reloadData();
	document.getElementById('timeWindow').value = '0';
}

function getTodayDate(separator = '') {
	const today = new Date();
	const year = today.getFullYear();
	const month = String(today.getMonth() + 1).padStart(2, '0');
	const day = String(today.getDate()).padStart(2, '0');
	return `${year}${separator}${month}${separator}${day}`;
}



//# %%%%%%%%%%%%%%%%%%%%%%%%%%% INDEXED DB %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

function indexDB_setup(deviceId) {
	const request = indexedDB.open('SensorDB', 1);

	request.onupgradeneeded = function (event) {
		const db = event.target.result;

		// Create store ONLY if it doesn't exist
		if (!db.objectStoreNames.contains(deviceId)) {
			const store = db.createObjectStore(deviceId, {
				keyPath: 'timestamp'  // Use timestamp as ID
			});
		}
	};

	// When database is ready
	request.onsuccess = function(e) {
		indexDB = e.target.result;
		// console.log('Database ready!');
	};

	request.onerror = function(e) {
		console.error('Database error:', e.target.error);
	};
}

function indexDB_putReading(deviceId, timestamp, temp, hum, lux) {
	if (!indexDB) {
		console.error('Database not ready');
		return;
	}
	
	const reading = {
		timestamp: timestamp,
		temp: temp,
		hum: hum,
		lux: lux
	};
	
	const tx = indexDB.transaction([deviceId], 'readwrite');
	const store = tx.objectStore(deviceId);
	const request = store.put(reading);			// ceate or overwrite
	
	request.onsuccess = function() {
		// console.log('Data saved with timestamp:', reading.timestamp);
	};
	
	request.onerror = function(e) {
		console.error('Error saving:', e.target.error);
	};
}


function indexDB_getReadings(deviceId, startTime, endTime) {
	if (!indexDB) {
		console.error('Database not ready');
		return;
	}
	
	const tx = indexDB.transaction([deviceId], 'readonly');
	const store = tx.objectStore(deviceId);
	
	// Create time range (timestamp is the key)
	const range = IDBKeyRange.bound(startTime, endTime);
	const request = store.getAll(range);
	
	request.onsuccess = function(e) {
		const readings = e.target.result;
		console.log(`Found ${readings.length} readings`);
		
		// Do something with data
		// readings.forEach(r => {
		// 	console.log(`${new Date(r.timestamp).toLocaleTimeString()}: ${r.temp}°C`);
		// });
	};
	
	request.onerror = function(e) {
		console.error('Error reading:', e.target.error);
	};
}