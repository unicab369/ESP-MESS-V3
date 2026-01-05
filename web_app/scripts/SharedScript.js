function get_field1() {
	return document.getElementById('field1-value')
}

function show_field_modal(title, field1, buttons_div) {
	document.getElementById('field-modal').style.display='block'

    document.getElementById('field-modal').innerHTML = /*html*/
		`<div class="w3-modal-content w3-container" style="max-width: 400px;">
			<h2>${title}</h2>

			<!-- New name input -->
			<div style="margin-bottom: 20px;">
				<!-- <label><b>Folder Name:</b></label> -->
				<input type="text" id="field1-value" value="${field1}" class="w3-input w3-border" 
						style="margin-top: 5px;" placeholder="Name" maxlength="16">
				<div id="field1-err" style="color: #f44336; font-size: 12px; margin-top: 5px; display: none;">
					Please enter a valid folder name
				</div>
			</div>

			<!-- Action buttons -->
			<div style="display: flex; gap: 10px; margin-top: 25px; margin-bottom: 15px;">
				${buttons_div}
			</div>
		</div>`
}

function show_textArea_modal(title, field1, area1, buttons_div) {
	document.getElementById('field-modal').style.display='block'

    document.getElementById('field-modal').innerHTML = /*html*/
		`<div class="w3-modal-content w3-container" style="max-width: 400px;">
			<h2>${title}</h2>

			<!-- New name input -->
			<div style="margin-bottom: 20px;">
				<!-- <label><b>Folder Name:</b></label> -->
				<input type="text" id="field1-value" value="${field1}" class="w3-input w3-border" 
						style="margin-top: 5px;" placeholder="Name" maxlength="16">
				<div id="field1-err" style="color: #f44336; font-size: 12px; margin-top: 5px; display: none;">
					Please enter a valid folder name
				</div>
			</div>
			
			<div style="margin-bottom: 20px;">
				<label style="display: block; margin-bottom: 5px; font-weight: 500;">String:</label>
				<textarea id="area1-value" class="w3-input w3-border" placeholder="Content"
						style="min-height: 200px; padding: 10px;" maxlength="500">
					${area1}
				</textarea>
				<div style="text-align: right; font-size: 12px; color: #666; margin-top: 5px;">
					0/500 characters
				</div>
			</div>

			<!-- Action buttons -->
			<div style="display: flex; gap: 10px; margin-top: 25px; margin-bottom: 15px;">
				${buttons_div}
			</div>
		</div>`
}

function close_field_modal() {
	document.getElementById('field-modal').style.display='none'
}