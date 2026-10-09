module.exports = [
  {
    "type": "heading",
    "defaultValue": "Analog Rainbow",
    "size": 3
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Features"
      },
      {
        "type": "text",
        "defaultValue": "Turn additional features on or off."
      },
      {
        "type": "toggle",
        "label": "Show weekday and month",
        "messageKey": "DataKeyDate",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show day of the month",
        "messageKey": "DataKeyDay",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show disconnected indicator",
        "messageKey": "DataKeyBT",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show battery level on shake (inner ring)",
        "messageKey": "DataKeyBattery",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show second hand (uses more power)",
        "messageKey": "DataKeySecondHand",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
];
