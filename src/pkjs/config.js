module.exports = [];

[
  { 
    "type": "heading", 
    "defaultValue": "Thin Configuration" ,
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
        "appKey": "DataKeyDate",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show day of the month",
        "appKey": "DataKeyDay",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show disconnected indicator",
        "appKey": "DataKeyBT",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show battery level (hour markers)",
        "appKey": "DataKeyBattery",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "label": "Show second hand (uses more power)",
        "appKey": "DataKeySecondHand",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save"
  }
]

