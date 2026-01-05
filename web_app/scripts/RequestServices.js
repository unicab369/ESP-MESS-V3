function get_serverIp() {
	const serverIp = document.getElementById('serverIp').value.trim()
	if (serverIp.length > 0) return serverIp
	alert('Please enter ESP32 IP address')
	return null
}

async function service_saveConfig(chart_id, config, onComplete) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	const params = new URLSearchParams({
		dev: chart_id,
		cfg: config					
	});
	console.log('%cSave config: %s', 'color: purple', params.toString());

	try {
		// Load config
		const resp = await fetch(`http://${serverIp}/s_config?${params.toString()}`, {
			method: 'GET',
		})

		if (resp.ok) {
			const text = await resp.text()
			console.log('save request:', text)
			onComplete?.()
		} else {
			const errorText = await resp.text()
			console.error('Server error:', errorText)
		}
	}
	catch(error) {
		console.error('Connection error:', error)
	}
}

async function service_startScan(onComplete) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	const params = new URLSearchParams({
		dev: "aa"		
	})

	scheduler.add(async () => {
		try {
			// Load config
			const resp = await fetch(`http://${serverIp}/scan?${params.toString()}`, {
				method: 'GET'
			})

			if (resp.ok) {
				const result = await resp.json()
				console.log('%cresult:', 'color: purple', result)
				onComplete?.(result)
			} else {
				const errorText = await resp.text()
				console.error('Server error:', errorText)
			}
		}
		catch(error) {
			console.error('Connection error:', error)
		}
	})
}

async function service_getDeviceLog(chart_id, onComplete) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	const params = new URLSearchParams({
		pa: chart_id,
		pb: 2026,
		pc: "new_1.bin"
	})

	try {
		// Load config
		const resp = await fetch(`http://${serverIp}/g_log?${params.toString()}`, {
			method: 'GET'
		})
		console.log('%crequest: %s', 'color: purple', resp.url)

		if (resp.ok) {
			const result = await resp.arrayBuffer()
			onComplete?.(result)
		} else {
			const errorText = await resp.text()
			console.error('Server error:', errorText)
		}
	}
	catch(error) {
		console.error('Connection error:', error)
	}
}

async function service_getFiles(entry, onComplete) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	const is_text = entry.toUpperCase().includes('.TXT')
	const params = new URLSearchParams({
		sub: entry,
		txt: is_text ? 1 : 0
	})

	try {
		// Load config
		const resp = await fetch(`http://${serverIp}/g_files?${params.toString()}`, {
			method: 'GET'
		})
		console.log('%crequest: %s', 'color: purple', resp.url)

		if (resp.ok) {
			const result = await (is_text ? resp.text() : resp.json())
			console.log('%cresult:', 'color: purple', result)
			onComplete?.(result)
		} else {
			const errorText = await resp.text()
			console.error('Server error:', errorText)
		}
	}
	catch(error) {
		console.error('Connection error:', error)	
	}
}

async function service_updateEntry(old_name, new_name, onComplete) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	const params = new URLSearchParams({
		old: old_name,
		new: new_name
	})

	try {
		// Load config
		const resp = await fetch(`http://${serverIp}/u_files?${params.toString()}`, {
			method: 'GET'
		})
		console.log('%crequest: %s', 'color: purple', resp.url)

		if (resp.ok) {
			const result = await (is_text ? resp.text() : resp.json())
			console.log('%cresult:', 'color: purple', result)
			onComplete?.(result)
		} else {
			const errorText = await resp.text()
			console.error('Server error:', errorText)
		}
	}
	catch(error) {
		console.error('Connection error:', error)
	}
}

async function service_createEntry(new_name, onComplete) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	const params = new URLSearchParams({
		new: new_name
	})

	try {
		// Load config
		const resp = await fetch(`http://${serverIp}/c_files?${params.toString()}`, {
			method: 'GET'
		})
		console.log('%crequest: %s', 'color: purple', resp.url)

		if (resp.ok) {
			const result = await (is_text ? resp.text() : resp.json())
			console.log('%cresult:', 'color: purple', result)
			onComplete?.(result)
		} else {
			const errorText = await resp.text()
			console.error('Server error:', errorText)
		}
	}
	catch(error) {
		console.error('Connection error:', error)
	}
}

async function service_updateNSV(namespace, key, value, type, onComplete) {
	const serverIp = get_serverIp()
	if (!serverIp) return

	const params = new URLSearchParams({
		name: namespace,
		key: key,
		val: value,
		typ: type
	})

	try {
		// Load config
		const resp = await fetch(`http://${serverIp}/u_nsv?${params.toString()}`, {
			method: 'GET'
		})
		console.log('%crequest: %s', 'color: purple', resp.url)

		if (resp.ok) {
			const result = await (is_text ? resp.text() : resp.json())
			console.log('%cresult:', 'color: purple', result)
			onComplete?.(result)
		} else {
			const errorText = await resp.text()
			console.error('Server error:', errorText)
		}
	}
	catch(error) {
		console.error('Connection error:', error)
	}
}
