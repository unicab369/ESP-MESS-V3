let devices = []

function updateDeviceList(device_caches, device_configs) {
	// Mark offline devices (older than 5 minutes)
	const fiveMinutesAgo = Date.now() - 300000
	devices = []
	
	device_caches.forEach(item => {
		const lastSeen = new Date(item[1]*1000);

		devices.push({
			uuid: item[0],
			config: device_configs.find(cfg => cfg[0] === item[0])?.[1] || 0,		// all falsy fallbacks to 0
			name: 'CH5xx',
			lastSeen: lastSeen,
			is_online: lastSeen.getTime() < fiveMinutesAgo,
		})
	})
	
	// Update UI
	if (devices.length === 0) {
		document.getElementById('devicesList').innerHTML = '<div style="text-align: center; padding: 20px; color: #666;">No devices found</div>';
	} else {
		let html = '';
		devices.forEach((item, index) => {
			const timeAgo = Math.round((Date.now() - item.lastSeen.getTime()) / 1000)
			const status_objs = {
				color: item.is_online ? '#4CAF50' : '#f44336',
				text: item.is_online ? 'Online' : 'Offline'
			}
			const logged_objs = {
				color: item.config > 0 ? '#4CAF50' : '#f44336',
				text: item.config > 0 ? 'Logging' : 'Not Log'
			}
			
			html += /*html*/
				`
					<div style="display: flex; align-items: center; padding: 8px; border-bottom: 1px solid #eee; gap: 10px;">
						<div style="flex: 1;">
							<div style="font-weight: bold;">${item.uuid}</div>
							<div style="font-size: 0.8rem; color: #666;">
								${item.name} • ${timeAgo}s ago
							</div>
						</div>
						<div onclick="showOptions('${index}')" style="background: ${logged_objs.color}; color: white; padding: 3px 8px; border-radius: 10px;">
							${logged_objs.text}
						</div>
						<div style="background: ${status_objs.color}; color: white; padding: 3px 8px; border-radius: 10px;">
							${status_objs.text}
						</div>
						<div onclick="showLog('${index}')" style="background: gray; color: white; padding: 3px 8px; border-radius: 10px;">
							Show Log
						</div>

						<div id="id01" class="w3-modal">
							<div class="w3-modal-content">
							<div class="w3-container">
								<span onclick="document.getElementById('id01').style.display='none'" 
									class="w3-button w3-display-topright">&times;
								</span>
								<h2>Device Log</h2>
								<p>Some text. Some text. Some text.</p>
							</div>
							</div>
						</div>
					</div>
				`
		})
		document.getElementById('devicesList').innerHTML = html
	}
	
	// Update counters
	const onlineCount = devices.filter(d => d.is_online);
	document.getElementById('devInfos-container').innerHTML = /*html*/
		`
			<div>Total: <span id="deviceCount">${devices.length}</span></div>
			<div>Online: <span id="onlineCount">${onlineCount}</span></div>
			<div>Last: <span id="lastScan">${(new Date()).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit'})}</span></div>
		`
}

function clearDevices() {
	if (devices.length < 1 || !confirm('Clear all devices?')) return
	devices = []
	updateDeviceList([])
}

function startScan() {
	service_startScan((result)=>{
		updateDeviceList(result.caches, result.cfgs)
	})
}

function showOptions(index) {
	const target = devices[index]
	console.log("target", target)

	threeButtonAlert('Configure Log',
		{
			name: 'Start Log', action: () => {
				if (target.config > 0) return
				console.log("update config: %s %d", target.uuid, 1000000102)

				// save default config 5 minutes time window, 2 seconds update window
				service_saveConfig(target.uuid, 1000000102, ()=>{
					startScan()
				})
			}
		},
		{
			name: 'Stop Log', action: () => {
				if (target.config === 0) return
				console.log("update config: %s %d", target.uuid, 0)

				service_saveConfig(target.uuid, 0, ()=>{
					startScan()
				})
			} 
		},
		{ name: 'Cancel', action: () => { } },
	)
}

function showLog(index) {
	const target = devices[index]
	console.log("target", target)

	let startTime = Date.now()

	service_getDeviceLog(target.uuid, (result)=>{
		const RECORD_SIZE = 10
		const recordCount = Math.floor(result.byteLength / RECORD_SIZE)
		const dataView = new DataView(result)
		console.log('Response time:', Date.now() - startTime, 'ms')
		console.log('Record count:', recordCount)

		let timestamp = 0
		for (let i = 0; i < recordCount; i++) {
			timestamp = dataView.getUint32(i * RECORD_SIZE, true)
			const value = dataView.getUint16(i * RECORD_SIZE + 4, true)
			const value2 = dataView.getUint16(i * RECORD_SIZE + 6, true)
			const value3 = dataView.getUint16(i * RECORD_SIZE + 8, true)
		}

		console.log("last Timestamp", timestamp)
		const date = new Date(timestamp*1000)
		console.log("last Time", date.toLocaleString())

		// document.getElementById('id01').style.display='block'
	})
}