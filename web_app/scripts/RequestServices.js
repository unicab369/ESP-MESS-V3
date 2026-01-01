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
	console.log('%cSave config: %s', 'color: red', params.toString());

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
				console.log('%cresult:', 'color: red', result)
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