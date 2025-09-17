# HVAC MQTT-Neon Bridge for Render.com

24/7 HVAC monitoring system that connects ESP32 sensors via MQTT to Neon PostgreSQL database.

## 🚀 Quick Deploy to Render.com

1. **Fork this repo** to your GitHub account
2. **Sign up** at [render.com](https://render.com) with GitHub (free)
3. **Create Web Service** → Connect this repo
4. **Configure**:
   - Build Command: `pip install -r requirements_render.txt`
   - Start Command: `python render_deploy_flask.py`
5. **Add Environment Variables** (see below)

## 🔧 Environment Variables

Add these in Render dashboard → Environment:

```
DATABASE_URL=postgresql://username:password@ep-xxx.us-east-1.aws.neon.tech/neondb?sslmode=require
MQTT_BROKER=f81277a8.ala.us-east-1.emqxsl.com
MQTT_PORT=8883
MQTT_USERNAME=your_mqtt_username
MQTT_PASSWORD=your_mqtt_password
MQTT_TOPIC_PREFIX=hvac/sensors
PORT=10000
```

## 📊 Features

- ✅ **Free hosting** on Render.com (750 hours/month)
- ✅ **Auto-reconnect** MQTT and database connections
- ✅ **Batch processing** for efficiency
- ✅ **Health endpoints** for monitoring
- ✅ **Alert system** for critical sensor values
- ✅ **Sleep/wake handling** (sleeps after 15min idle)

## 🔍 Monitoring

- **Health**: `https://your-app.onrender.com/health`
- **Stats**: `https://your-app.onrender.com/stats`
- **Wake**: `https://your-app.onrender.com/wake`

## 💡 Keep-Alive (Optional)

To prevent sleep, set up a cron job:
```bash
*/10 * * * * curl https://your-app.onrender.com/health
```

## 🏗️ Architecture

ESP32 → MQTT Broker → Render.com Bridge → Neon PostgreSQL
