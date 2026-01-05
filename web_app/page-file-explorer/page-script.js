var PATH_ENTRIES = []
var folders_files = []

const nvs_type_map = {
	1: 'u8', 2: 'u16', 4: 'u32', 8: 'u64',
	17: 'i8', 18: 'i16', 20: 'i32', 24: 'i64',
	33: 'str', 66: 'blob', 255: 'any'
}

function backEntry() {
	if (PATH_ENTRIES.length < 2) return
	PATH_ENTRIES.pop()
	reloadEntry('')
}

function onEraseNSV(old_key) {
	const namespace = get_field1().value.trim() ?? ''
	if (namespace.length < 1) {
		alert('Invalid Namespace');
		return
	}

	if (old_key?.length > 0) {
		if (confirm('Are you sure you want to delete this Key Pair?')) {
			// Delete Key
			service_eraseNVS(namespace, old_key, (result) => {
				close_field_modal()
				reloadNVS()
			})
		}
		return
	}

	if (confirm('Are you sure you want to delete this Namespace?')) {
		// Delete Namespace
		service_eraseNVS(namespace, '', (result) => {
			close_field_modal()
			reloadNVS()
		})
	}
}

function reloadNVS(sub_entry = 'nvs') {
	service_getFiles(sub_entry, (result) => {
		const sorted = result.sort((a, b) => {
			return (a[0]+a[1]).toUpperCase().localeCompare((b[0]+b[1]).toUpperCase())
		})

		let html = ''
		if (sorted.length === 0) {
			html += '<div style="text-align: center; padding: 20px; color: #666;">No Files found</div>';
		} else {
			// For each cell
			sorted.forEach((item, index) => {
				html += /*html*/
					`
						<div onclick="onEditNVS('${item}')" 
							style="display: flex; align-items: center; padding: 8px; border-bottom: 1px solid #eee; gap: 10px;">
							<div style="flex: 1;">${item[0]}/${item[1]} (${nvs_type_map[item[2]]})</div>

							<div style="background: gray; color: white; padding: 5px 12px; border-radius: 10px;">
								✎ Edit
							</div>
						</div>
					`
			})
		}
		document.getElementById('list-container').innerHTML = html

		document.getElementById('button-container').innerHTML = /*html*/
			`<button class="btn" onclick="onEditNVS('')">📜 Add Key Pair</button>
			<button class="btn" style="color: red;" onclick="showEraseNSV()">🗑️ Erase Namespace</button>`
	})
}


function onEditNVS(target) {
	console.log("onEditNVS:", target)
	const is_create = target.length < 1
	const splits = target?.split(',')

	if (!is_create) {
		const namespace = splits[0]
		const new_key = splits[1]
		const type = splits[2]

		// Get
		service_requestNVS(namespace, new_key, '', 0, type, (result) => {
			showNVSForm(namespace, new_key, result.val, type)
		})

		return
	}
	else {
		showNVSForm()
	}
}

function onUpdateNVS(old_namespace, old_key) {
	const namespace = document.getElementById('field1-value').value.trim()
	const new_key = document.getElementById('field2-value').value.trim()
	const value = document.getElementById('field3-value').value.trim()
	const type = document.getElementById('select-value').value
	console.log("onUpdateNVS:", namespace, new_key, value, type)
	
	if (namespace.length == 0 || new_key.length == 0 || value.length == 0 || !type) {
		alert('Please enter Required values')
		return
	}

	// Update
	service_requestNVS(namespace, new_key, old_key, value, type, (result) => {
		close_field_modal()
		reloadNVS()
	})
}

function reloadEntry(sub_entry = '*sdcard*log', restart = true) {
	if (sub_entry.length > 0)  {
		if (restart) {
			PATH_ENTRIES = [sub_entry]
		} else {
			PATH_ENTRIES.push(sub_entry)
		}
	}

	console.log("entries:", PATH_ENTRIES)
	sub_entry = PATH_ENTRIES.join('*')
	console.log("sub_entry:", sub_entry)

	service_getFiles(sub_entry, (result) => {
		let html = /*html*/
			`<div onclick="backEntry()" style="display: flex; align-items: center; padding: 8px; 
												border-bottom: 1px solid #eee; gap: 10px;">
				<div>⬅️ Back</div>
			</div>`

		const is_text = sub_entry.toUpperCase().includes('.TXT')

		if (is_text) {
			html += /*html*/
				`<div style="display: flex; align-items: center; padding: 8px; border-bottom: 1px solid #eee; gap: 10px;">
						<div style="white-space: pre-wrap;">${result}</div>
				</div>`
		}
		else {
			// show error if there are more than 1 PATH_ENTRIES
			if (PATH_ENTRIES.length < 2) html = ``
			
			// Sort folders first
			folders_files = result.sort((a, b) => {
				const aIsFolder = !a.includes('.')
				const bIsFolder = !b.includes('.')
				
				// If one is folder and other isn't, folder comes first
				if (aIsFolder && !bIsFolder) return -1
				if (!aIsFolder && bIsFolder) return 1
				
				// Both are same type, sort alphabetically
				return a.localeCompare(b)
			})

			// Update UI
			if (folders_files.length === 0) {
				html += '<div style="text-align: center; padding: 20px; color: #666;">No Files found</div>';
			} else {
				// For each cell
				folders_files.forEach((item, index) => {
					html += /*html*/
						`<div onclick="reloadEntry('${item}', false)" 
								style="display: flex; align-items: center; padding: 8px; border-bottom: 1px solid #eee; gap: 10px;">
							<div style="flex: 1;">${item.includes('.') ? '📄' : '📁'} ${item}</div>

							<div onclick="event.stopPropagation(); onEditEntry('${index}')" 
								style="background: gray; color: white; padding: 5px 12px; border-radius: 10px;">
								✎ Edit
							</div>
						</div>`
				})
			}
		}
		
		document.getElementById('list-container').innerHTML = html
		document.getElementById('button-container').innerHTML = /*html*/
			`<button class="btn" onclick="onCreateEntry(0)">📂 Add Folder</button>
			<button class="btn" onclick="onCreateEntry(1)">📄 Add File</button>`
	})
}


function onCreateEntry(is_file) {
	if (is_file) {
		show_textArea_modal('Create File', '', '', `
			<button onclick="handleUpdateEntry('', 0)" class="w3-button w3-blue w3-round" style="flex: 1;">
				Create
			</button>
			
			<button onclick="close_field_modal()" class="w3-button w3-gray w3-round" style="flex: 1;">
				Cancel
			</button>`
		)
	}
	else {
		show_field_modal('Create Folder', '', `
			<button onclick="handleUpdateEntry('', 0)" class="w3-button w3-blue w3-round" style="flex: 1;">
				Create
			</button>
			
			<button onclick="close_field_modal()" class="w3-button w3-gray w3-round" style="flex: 1;">
				Cancel
			</button>`
		)	
	}
}

function onEditEntry(index) {
	const old_name = folders_files[index]
	const title_str = old_name.includes('.') ? 'Edit File' : 'Edit Folder'
	const buttons_div = /*html*/
		`<button onclick="handleUpdateEntry('${old_name}', 0)" class="w3-button w3-blue w3-round" style="flex: 1;">
			Update
		</button>
		
		<button onclick="handleUpdateEntry('${old_name}', 1)" class="w3-button w3-red w3-round" style="flex: 1;">
			Delete
		</button>
		
		<button onclick="close_field_modal()" class="w3-button w3-gray w3-round" style="flex: 1;">
			Cancel
		</button>`
	show_field_modal(title_str, old_name, buttons_div)
}

function handleModifyFile() {
	const new_name = document.getElementById('field1-value').value.trim()
}

function handleUpdateEntry(old_name, is_delete = false) {
	console.log('old_name:', old_name)
	console.log('entries:', PATH_ENTRIES)

	const is_file = old_name.includes('.')
	const new_name = document.getElementById('field1-value').value.trim()
	const old_splits = old_name.split('.')
	const new_splits = new_name.split('.')

	if (
		is_file && new_splits.length !== 2 && new_splits[1] != old_splits[1] ||		// check file format
		!is_file && new_splits.length !== 1	||										// check folder format
		new_name.length < 1
	) {
		alert('Invalid: name or format mismatch')
		return
	}

	const firstEntry = PATH_ENTRIES.find(() => true);
	const newPath = PATH_ENTRIES.join('*') + '*' + new_name
	const oldPath = PATH_ENTRIES.join('*') + '*' + old_name
	console.log("update: %s %s", newPath, oldPath)

	if (is_delete) {
		if (confirm(`Are you sure you want to delete ${old_name}?`)) {
			// Delete
			service_updateEntry('', oldPath, is_file, (result) => {
				reloadEntry(firstEntry)
				close_field_modal()
				return
			})
		}
		return
	}

	if (old_name.length < 1) {
		// Create
		service_updateEntry(newPath, '', is_file, (result) => {
			reloadEntry(firstEntry)
			close_field_modal()
		})
		return
	}

	// No change
	if (old_name === new_name) {
		close_field_modal()
		return
	}

	// Update
	service_updateEntry(newPath, oldPath, is_file, (result) => {
		reloadEntry(firstEntry)
		close_field_modal()
	})
}