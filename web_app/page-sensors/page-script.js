let indexDB = null;
let chartObjs = {};

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

function get_updateInterval(chart_id) {
	return document.getElementById(`updateInterval-${ chart_id }`);
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
// Response format: [["AABBCCDD", uint32_t], ["AABBCCDA", uint32_t], ...]
function initCharts(arrays) {
	chartObjs = {}
	let output = ``

	for (const values of arrays) {
		const chart_id = values[0]

		chartObjs[chart_id] = {
			scheduler: null,
			time_window_idx: Math.floor(values[1] / Math.pow(10, 2)) % 100,	// 3rd and 4th digits from right,
			update_window_idx: Math.floor(values[1] / Math.pow(10, 0)) % 100,	// last 2 digits
			record_min_time: 0,
			record_max_time: 0,
			custom_timestamp: null
		}

		output +=  /*html*/
			`<div class="chart-card">
				<div class="chart-title">📈 Node: ${ chart_id.toUpperCase() }</div>
				
				<div class="chart-controls">
					<label for="timeWindow-${ chart_id }">Time Window:</label>
					<select id="timeWindow-${ chart_id }" onchange="start_update_scheduler('${chart_id}'); esp_saveConfig('${chart_id}')">
						${ make_options(
							chartObjs[chart_id].time_window_idx ?? appConfig.time_window_default_idx,
							appConfig.time_window_strs,
							appConfig.time_window_mins
						) }
					</select>
					<button class="btn" onclick="get_timeWindow('${chart_id}').value = '1'">Reset</button>

					<label for="updateInterval-${ chart_id }">Update Interval:</label>
					<select id="updateInterval-${ chart_id }" onchange="start_update_scheduler('${chart_id}'); esp_saveConfig('${chart_id}')">
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

			hooks: {
				setSelect: [
					(u) => {
						// this get triggers every time user finishes a selection
						// however it only return the current scale and not the selected scale
						const min = u.scales.x.min
						const max = u.scales.x.max
						console.log('%cUser selected:', 'color: orange', min, max)
						chartObjs[chart_id].custom_timestamp = Date.now()/1000
					}
				],
				setScale: [
					(u, scaleKey, scaleMin, scaleMax) => {
						// this get triggers every time .setScale() is called
						// this returns the selected scale
						if (scaleKey !== 'x') return
						const min = u.scales.x.min
						const max = u.scales.x.max
						chartObjs[chart_id].record_min_time = min
						chartObjs[chart_id].record_max_time = max

						// let start = new Date(min*1000).toLocaleString()
						// let end = new Date(max*1000).toLocaleString()
						// console.log('%cUser zoomed:', 'color: orange', start, end);
					}
				]
			},
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
		start_update_scheduler(chart_id);
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
		const updateInterval = appConfig.update_window_ms.indexOf(Number(get_updateInterval(chart_id).value))
		console.log('%ctimeWindow:%d updateInterval:%d', 'color: red', timeWindow, updateInterval)

		if (timeWindow > -1 && timeWindow < appConfig.time_window_mins.length &&
			updateInterval > -1 && updateInterval < appConfig.update_window_ms.length) {
			config = 1E9 + timeWindow*100 + updateInterval
		}

		service_saveConfig(chart_id, config)
	})
}

//! reload charts data
async function reload_records(chart_id) {
	function update_timeWindow(chart_id) {
		let xMin = chartObjs[chart_id].record_min_time
		let xMax = chartObjs[chart_id].record_max_time
		const custom_timestamp = chartObjs[chart_id].custom_timestamp

		// Apply custom range
		if (custom_timestamp) {
			const elapsed_s = Date.now()/1000 - custom_timestamp
			const new_minX = xMin + elapsed_s
			const new_maxX = xMax + elapsed_s
			chartObjs[chart_id].custom_start_time = new_minX
			chartObjs[chart_id].custom_end_time = new_maxX
			chartObjs[chart_id].custom_timestamp = Date.now()/1000
			chartObjs[chart_id].plot.setScale('x', { min: new_minX, max: new_maxX })
			return
		}

		const seconds = Number(get_timeWindow(chart_id).value) * 60

		if (seconds > 0) {
			xMin = xMax - seconds
		}

		// Apply zoom
		chartObjs[chart_id].plot.setScale('x', { min: xMin, max: xMax })
	}

	const serverIp = get_serverIp()
	if (!serverIp) return

	scheduler.add(async () => {
		const today = new Date()
		let startTime = Date.now()

		const params = new URLSearchParams({
			dev: chart_id,									// device
			yr: today.getFullYear(),						// year
			mth: today.getMonth() + 1,						// month
			day: today.getDate(),							// day
			win: get_timeWindow(chart_id).value, 			// time window
			minT: 1767886318,	// min time
			maxT: chartObjs[chart_id].record_max_time || 0,	// max time
		})
		// indexDB_setup(chart_id)

		try {
			const resp = await fetch(`http://${serverIp}/g_rec?${params.toString()}`, {
				method: 'GET'
			})
			console.log('%creload_records: %s', 'color: purple', resp.url)

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
			
			const time_dif_ms = Date.now() - startTime
			console.log(`count: ${recordCount} records ${time_dif_ms} ms`)

			// Parse into combined array directly
			const records = [];
			for (let i = 0; i < recordCount; i++) {
				records.push({
					time: dataView.getUint32(i * RECORD_SIZE, true),
					temp: dataView.getUint16(i * RECORD_SIZE + 4, true),
					hum: dataView.getInt16(i * RECORD_SIZE + 6, true),
					lux: dataView.getUint16(i * RECORD_SIZE + 8, true)
				})
				const target = records[i]
				// console.log(`Record ${i}: Time=${target.time}, Temp=${target.temp}`)
				// console.log(`Record ${i}: Time=${new Date(target.time*1000).toLocaleString()}, Temp=${target.temp}`)
			}

			// const target = records.at(-1)
			// console.log(`Record ${recordCount}: Time=${target.time}, Temp=${target.temp}`)

			//# Sort by time - uPlot requires ascending order
			records.sort((a, b) => a.time - b.time);

			// Extract arrays
			const timeStampArr = records.map(r => r.time);
			const tempArr = records.map(r => r.temp);
			const humArr = records.map(r => r.hum);
			const luxArr = records.map(r => r.lux);

			// Dont update min/max if custom range is used
			if (!chartObjs[chart_id].custom_timestamp) {
				chartObjs[chart_id].record_min_time = timeStampArr[0] || 0
				chartObjs[chart_id].record_max_time = timeStampArr.at(-1) || 0
			}

			chartObjs[chart_id].plot.setData([timeStampArr, tempArr, humArr, luxArr])
			update_timeWindow(chart_id)

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


function getTodayDate(separator = '') {
	const today = new Date()
	const year = today.getFullYear()
	const month = String(today.getMonth() + 1).padStart(2, '0')
	const day = String(today.getDate()).padStart(2, '0')
	return `${year}${separator}${month}${separator}${day}`
}

function start_update_scheduler(chart_id) {
	// clear custom range
	chartObjs[chart_id].custom_timestamp = null

	//! IMPORTANT: clear scheduler
	clearInterval(chartObjs[chart_id].scheduler)

	const value = Number(get_updateInterval(chart_id).value)
	if (value <= 0) return

	// Update immediately once
	reload_records(chart_id);
	
	// Set up interval
	chartObjs[chart_id].scheduler = setInterval(() => {
		reload_records(chart_id);
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