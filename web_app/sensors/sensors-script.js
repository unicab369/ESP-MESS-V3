let indexDB = null;
let chartObjs = {};

// let chartObjs = {
// 	'aabbccdd': {
// 		plot: null,
// 		scheduler: null,
// 		config: {
// 			time_window_idx: 0,
// 			update_window_idx: 1
// 		}
// 	},
// };

let appConfig = {
	'time_window_default_idx': 1,
	'time_window_mins': [1, 5, 20, 60, 300, 720, 1440, 4320, 10080, 43200, 129600, 259200, 0],
	'time_window_strs': ['1 minute', '5 minutes', '20 minutes', '1 hour', '5 hours', '12 hours',
						'1 day', '3 days', '1 week', '1 month', '3 months', '6 months', '0 (all)'],

	'update_window_default_idx': 1,
	'update_window_ms': [1000, 2000, 5000, 10000, 30000, 0],
	'update_window_strs': ['1 second', '2 seconds', '5 seconds', '10 seconds', '30 seconds', 'Pause']
}

function get_timeWindow(chart_id) {
	return document.getElementById(`timeWindow-${ chart_id }`);
}

function get_updateWindow(chart_id) {
	return document.getElementById(`updateWindow-${ chart_id }`);
}

function make_options(default_idx, option_strs, option_values) {
	// clamp and validate value
	let selected_idx = default_idx ?? 0
	selected_idx = Math.min(selected_idx, option_strs.length - 1)

	return option_strs.map((str, idx) => {
		return `<option value="${option_values[idx]}"
					${idx === selected_idx ? 'selected' : ''}>
					${str}
				</option>`
	}).join('')
}

//! Initialize charts
// Response format: [["AABBCCDD", uint32_t], ["11223344", uint32_t], ...]
function initCharts(arrays) {
	chartObjs = {}
	let output = ``

	for (const values of arrays) {
		const chart_id = values[0]

		chartObjs[chart_id] = {}
		chartObjs[chart_id].scheduler = null
		chartObjs[chart_id].time_window_idx = Math.floor(values[1] / Math.pow(10, 2)) % 100;	// 3rd and 4th digits from right
		chartObjs[chart_id].update_window_idx = Math.floor(values[1] / Math.pow(10, 0)) % 100;	// last 2 digits

		output +=  /*html*/
			`<div class="chart-card">
				<div class="chart-title">📈 Node: ${ chart_id.toUpperCase() }</div>
				
				<div class="chart-controls">
					<label for="timeWindow-${ chart_id }">Time Window:</label>
					<select id="timeWindow-${ chart_id }" onchange="set_timeWindow('${chart_id}'); esp_saveConfig('${chart_id}')">
						${ make_options(
							chartObjs[chart_id].time_window_idx ?? appConfig.time_window_default_idx,
							appConfig.time_window_strs,
							appConfig.time_window_mins
						) }
					</select>
					<button class="btn" onclick="get_timeWindow('${chart_id}').value = '1'">Reset</button>

					<label for="updateWindow-${ chart_id }">Update Window:</label>
					<select id="updateWindow-${ chart_id }" onchange="set_updateWindow('${chart_id}'); esp_saveConfig('${chart_id}')">
						${ make_options(
							chartObjs[chart_id].update_window_idx ?? appConfig.update_window_default_idx,
							appConfig.update_window_strs,
							appConfig.update_window_ms
						) }
					</select>
					<button class="btn" onclick="clearInterval(chartObjs['${chart_id}'].scheduler)">Pause</button>
				</div>

				<div class="chart-wrapper">
					<div id="chart-${chart_id}"></div>
				</div>
			</div>
			`
	}

	document.getElementById('charts-container').innerHTML = output

	for (const values of arrays) {
		const chart_id = values[0]

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

		chartObjs[chart_id].plot = new uPlot(chartOptions, [], document.getElementById(`chart-${chart_id}`))
		set_updateWindow(chart_id);
	}
}

// Connect to ESP32 server
async function connectToServer() {
	const serverIp = get_serverIp()
	if (!serverIp) return

	try {
		// Test connection
		const response = await fetch(`http://${serverIp}/info`, { method: 'GET' })
		if (response.ok) {
			const data = await response.json()
			document.getElementById('espIp').textContent = data.ip_address
			document.getElementById('freeMemory').textContent = data.heap_free + ' bytes'
			console.log('Connected to:', data)
			
			// Start live data
			startLiveData()
		} else {
			throw new Error(`HTTP ${response.status}`)
		}
	} catch (error) {
		console.error('Connection error:', error)
	}
}

//! save charts configs
async function esp_saveConfig(chart_id) {
	scheduler.add(async () => {
		let config = 1000000101	// Default value (5 minutes, 2 seconds)
		const timeWindow = appConfig.time_window_mins.indexOf(Number(get_timeWindow(chart_id).value))
		const updateWindow = appConfig.update_window_ms.indexOf(Number(get_updateWindow(chart_id).value))
		console.log('%ctimeWindow:%d updateWindow:%d', 'color: red', timeWindow, updateWindow)

		if (timeWindow > -1 && timeWindow < appConfig.time_window_mins.length &&
			updateWindow > -1 && updateWindow < appConfig.update_window_ms.length) {
			config = 1E9 + timeWindow*100 + updateWindow
		}

		service_saveConfig(chart_id, config)
	})
}

//! reload charts data
async function esp_reloadData(chart_id) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	scheduler.add(async () => {
		const today = new Date()
		let startTime = Date.now()

		const params = new URLSearchParams({
			dev: chart_id,							// device
			yr: today.getFullYear(),				// year
			mth: today.getMonth() + 1,				// month
			day: today.getDate(),					// day
			win: get_timeWindow(chart_id).value 	// time window
		})
		console.log('Fetching data:', params.toString())
		// indexDB_setup(chart_id)

		try {
			const resp = await fetch(`http://${serverIp}/data?${params.toString()}`, {
				method: 'GET'
			})

			if (!resp.ok) {
				const errorText = await resp.text()
				console.error('Server error:', errorText)
				console.log("STOP SCHEDULER. chart_id:", chart_id)
				clearInterval(chartObjs[chart_id].scheduler)
				throw new Error(`HTTP ${resp.status}: ${errorText}`)
			}

			const buffer = await resp.arrayBuffer()
			const RECORD_SIZE = 10
			const recordCount = Math.floor(buffer.byteLength / RECORD_SIZE)
			const dataView = new DataView(buffer)
			console.log('Response time:', Date.now() - startTime, 'ms')

			const timeStampArr = new Array(recordCount)
			const tempArr = new Array(recordCount)
			const humArr = new Array(recordCount)
			const luxArr = new Array(recordCount)

			// Pre-allocate arrays
			for (let i = 0; i < recordCount; i++) {
				timeStampArr[i] = dataView.getUint32(i * RECORD_SIZE, true)
				tempArr[i] = dataView.getUint16(i * RECORD_SIZE + 4, true)
				humArr[i] = dataView.getInt16(i * RECORD_SIZE + 6, true)
				luxArr[i] = dataView.getUint16(i * RECORD_SIZE + 8, true)
			}

			console.log('count:', recordCount)
			chartObjs[chart_id].plot.setData([timeStampArr, tempArr, humArr, luxArr])
			set_timeWindow(chart_id)

		} catch (error) {
			console.error('Request failed:', error)
			if (chartObjs[chart_id]?.scheduler) {
				clearInterval(chartObjs[chart_id].scheduler)
			}
			throw error
		}
	})
}


//# %%%%%%%%%%%%%%%%%%%%%%%%%%% TIME WINDOW %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

function set_timeWindow(chart_id) {
	const timestamps = chartObjs[chart_id].plot.data[0];
	if (timestamps.length === 0) return;
	
	let xMin, xMax;
	const minutes = parseInt(get_timeWindow(chart_id).value);

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
	chartObjs[chart_id].plot.setScale('x', { min: xMin, max: xMax });
}

function getTodayDate(separator = '') {
	const today = new Date();
	const year = today.getFullYear();
	const month = String(today.getMonth() + 1).padStart(2, '0');
	const day = String(today.getDate()).padStart(2, '0');
	return `${year}${separator}${month}${separator}${day}`;
}


//# %%%%%%%%%%%%%%%%%%%%%%%%%%% UPDATE WINDOW %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

function set_updateWindow(chart_id) {
	const value = parseInt(get_updateWindow(chart_id).value)
	if (value <= 0) return

	//! IMPORTANT: clear scheduler
	clearInterval(chartObjs[chart_id].scheduler)

	// Update immediately once
	esp_reloadData(chart_id);
	
	// Set up interval
	chartObjs[chart_id].scheduler = setInterval(() => {
		esp_reloadData(chart_id);
	}, value);
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