var entries = []
var folders_files = []
var selected_item = ''

const nvs_type_map = {
	1: 'u8', 2: 'u16', 4: 'u32', 8: 'u64',
	17: 'i8', 18: 'i16', 20: 'i32', 24: 'i64',
	33: 'str', 66: 'blob', 255: 'any'
}

function backEntry() {
	if (entries.length < 2) return
	entries.pop()
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

function reloadEntry(sub_entry = '*sdcard*log', is_new = true) {
	if (sub_entry.length > 0)  {
		if (is_new) {
			entries = [sub_entry]
		} else {
			entries.push(sub_entry)
		}
	}

	console.log("entries:", entries)
	sub_entry = entries.join('*')
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
			// show error if there are more than 1 entries
			if (entries.length < 2) html = ``
			
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
	const buttons_div = /*html*/
		`<button onclick="onUpdateEntry(1)" class="w3-button w3-blue w3-round" style="flex: 1;">
			Create
		</button>
		
		<button onclick="close_field_modal()" class="w3-button w3-gray w3-round" style="flex: 1;">
			Cancel
		</button>`
	show_field_modal(is_file ? 'Create File' : 'Create Folder', '', buttons_div)
}

function onEditEntry(index) {
	selected_item = folders_files[index]
	const title_str = selected_item.includes('.') ? 'Edit File' : 'Edit Folder'
	const buttons_div = /*html*/
		`<button onclick="onUpdateEntry(0)" class="w3-button w3-blue w3-round" style="flex: 1;">
			Update
		</button>
		
		<button onclick="onUpdateEntry(1)" class="w3-button w3-red w3-round" style="flex: 1;">
			Delete
		</button>
		
		<button onclick="close_field_modal()" class="w3-button w3-gray w3-round" style="flex: 1;">
			Cancel
		</button>`
	show_field_modal(title_str, selected_item, buttons_div)
}

function onUpdateEntry(is_delete = 0) {
	if (!selected_item) return
	const is_file = selected_item.includes('.')
	const new_name = document.getElementById('field1-value').value
	const old_splits = selected_item.split('.')
	const new_splits = new_name.split('.')

	if (is_file && new_splits.length !== 2 && new_splits[1] != old_splits[1] ||
		!is_file && new_splits.length !== 1
	) {
		alert('Invalid: new name format mismatch')
		return
	}

	if (is_delete && confirm(`Are you sure you want to delete this ${is_file ? 'File' : 'Folder'}?`)) {
		service_updateEntry(selected_item, '', (result) => {
			close_field_modal()
		})

		return
	}

	// Update
	service_updateEntry(selected_item, new_name, (result) => {
		close_field_modal()
	})
}