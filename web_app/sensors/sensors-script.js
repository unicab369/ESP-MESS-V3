// Global variables
let sensorChart2 = null;
let liveDataInterval = null;
let updateInterval = 1000; // 1 second
let dataPoints = [];
let maxPoints = 100;
let indexDB = null;

let chartIds = ['aabbccdd', 'aabbccda'];
let charts = [];

function get_timeWindow() {
	return document.getElementById('timeWindow');
}

// Initialize charts
function initCharts() {
	let output = ``

	for (const chart_id of chartIds) {
		output +=  /*html*/
			`<div class="chart-card">
				<div class="chart-title">📈 Node: ${ chart_id }</div>
				
				<div class="chart-controls">
					<label for="timeWindow">Time Window:</label>
					<select id="timeWindow" onchange="timeWindow_apply()">
						<option value="1">1 minute</option>
						<option value="5" selected>5 minutes</option>
						<option value="20">20 minutes</option>
						<option value="60">1 hour</option>
						<option value="300">5 hours</option>
						<option value="720">12 hours</option>
						<option value="1440">1 day</option>
						<option value="4320">3 days</option>
						<option value="10080">7 days</option>
						<option value="43200">1 month</option>
						<option value="129600">3 months</option>
						<option value="259200">6 months</option>
						<option value="0">All data</option>
					</select>
					<button class="btn" onclick="timeWindow_reset()">Reset</button>

					<label for="updateWindow">Update Window:</label>
					<select id="updateWindow" onchange="updateWindow_apply()">
						<option value="1000">1 second</option>
						<option value="2000" selected>2 seconds</option>
						<option value="5000">5 seconds</option>
						<option value="10000">10 seconds</option>
						<option value="30000">30 seconds</option>
					</select>
					<button class="btn" onclick="updateWindow_pause()">Pause</button>
				</div>

				<div class="chart-wrapper">
					<div id="chart-${chart_id}"></div>
				</div>
			</div>
			`
	}

	document.getElementById('charts-container').innerHTML = output

	for (const chart_id of chartIds) {
		const chartOptions = {
			width: document.getElementById(`chart-${chart_id}`).offsetWidth,
			height: 250,

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
					grid: { stroke: "#eee" }
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
					width: 1.5,
					value: (u, v) => v?.toFixed(1) + "°C"
				},
				{
					scale: "y",
					label: "Hum(%)",
					stroke: "#4444ff",
					width: 1.5,
					value: (u, v) => v?.toFixed(1) + "%"
				},
				{
					scale: "y2",
					label: "Lux",
					stroke: "#36454F",
					width: 1.5,
					value: (u, v) => v?.toFixed(0) + " lux"
				}
			]
		};

		charts.push(new uPlot(chartOptions, [], document.getElementById(`chart-${chart_id}`)))
	}

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
		console.error('Connection error:', error);
	}
}


async function reloadData(dateStr = null) {
	const serverIp = document.getElementById('serverIp').value.trim();
	if (!serverIp) {
		alert('Please enter ESP32 IP address');
		return;
	}

	for (let i=0; i<charts.length; i++) {
		let deviceId = 'aabbccdd';

		const params = new URLSearchParams({
			dev: deviceId,					// device
			yr: 2025,						// year
			mth: 12,						// month
			day: 30,						// day
			win: get_timeWindow().value 	// time window
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
				}

				// console.log('Sensor data:', sensorData);
				console.log('count:', recordCount);
				charts[i].setData([timeStampArr, tempArr, humArr, luxArr]);
				timeWindow_apply();
			} else {
				throw new Error(`HTTP ${response.status}`);
			}
		} catch (error) {
			console.error('Connection error:', error);
		}	
	}
}


//# %%%%%%%%%%%%%%%%%%%%%%%%%%% TIME WINDOW %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

function timeWindow_apply() {
	const minutes = parseInt(get_timeWindow().value);
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
	get_timeWindow().value = '1';
}

function getTodayDate(separator = '') {
	const today = new Date();
	const year = today.getFullYear();
	const month = String(today.getMonth() + 1).padStart(2, '0');
	const day = String(today.getDate()).padStart(2, '0');
	return `${year}${separator}${month}${separator}${day}`;
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