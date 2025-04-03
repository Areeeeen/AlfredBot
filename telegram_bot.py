import telebot
import requests

TELEGRAM_BOT_TOKEN = "YOUR_TELEGRAM_BOT_TOKEN"
FIREBASE_URL = "https://your-firebase-url.firebaseio.com/devices/"

bot = telebot.TeleBot(TELEGRAM_BOT_TOKEN)

@bot.message_handler(commands=['feed'])
def send_command(message):
    parts = message.text.split()
    if len(parts) < 2:
        bot.reply_to(message, "⚠️ Please specify a device ID! Example: /feed device_1")
        return
    
    device_id = parts[1]
    response = requests.put(FIREBASE_URL + device_id + "/feed.json", json=True)
    
    if response.status_code == 200:
        bot.reply_to(message, f"✅ Food dispensing for {device_id}!")
    else:
        bot.reply_to(message, "⚠️ Failed to update Firebase!")

bot.polling()
