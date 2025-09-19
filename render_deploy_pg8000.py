"""
Render.com Flask-based MQTT-Neon Bridge with pg8000 (pure Python PostgreSQL driver)
Uses pg8000 for Python 3.13 compatibility - no C extensions, no compilation issues.
"""

import json
import ssl
import threading
import time
import logging
import os
from datetime import datetime
from dateutil import parser
import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from flask import Flask, jsonify
from flask_cors import CORS
import pg8000.native

# Load environment variables
load_dotenv()

# Setup logging for Render
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[logging.StreamHandler()]
)
logger = logging.getLogger(__name__)

class RenderPG8000MQTTBridge:
    def __init__(self):
        """Initialize the MQTT-Neon bridge."""
        self.app = Flask(__name__)
        CORS(self.app)
        
        # Load environment variables
        load_dotenv()
        
        # Configuration
        self.database_url = os.getenv('DATABASE_URL')
        self.mqtt_broker = os.getenv('MQTT_BROKER', 'f81277a8.ala.us-east-1.emqxsl.com')
        self.mqtt_port = int(os.getenv('MQTT_PORT', 8883))
        self.mqtt_username = os.getenv('MQTT_USERNAME', 'admin')
        self.mqtt_password = os.getenv('MQTT_PASSWORD', 'admin123')
        self.topic_prefix = os.getenv('MQTT_TOPIC_PREFIX', 'hvac/sensors')
        self.port = int(os.getenv('PORT', 10000))
        
        # State variables
        self.db_connection_params = None
        self.mqtt_client = None
        self.connected = False
        self.message_queue = []
        self.last_activity = time.time()
        self.batch_size = 20
        self.batch_timeout = 2
        self.last_keepalive = time.time()
        self.keepalive_interval = 300  # 5 minutes
        
        self.setup_mqtt()
        self.setup_flask_routes()
        self.parse_database_url()
    
    def parse_database_url(self):
        """Parse DATABASE_URL for pg8000 connection."""
        try:
            # Parse postgresql://user:password@host:port/database?options
            url = self.database_url
            if url.startswith('postgresql://'):
                url = url[13:]  # Remove postgresql://
            
            # Split user:password@host:port/database
            auth_host, database = url.split('/')
            database = database.split('?')[0]  # Remove query parameters
            
            if '@' in auth_host:
                auth, host_port = auth_host.split('@')
                user, password = auth.split(':')
            else:
                host_port = auth_host
                user = password = None
            
            if ':' in host_port:
                host, port = host_port.split(':')
                port = int(port)
            else:
                host = host_port
                port = 5432
            
            self.db_connection_params = {
                'host': host,
                'port': port,
                'database': database,
                'user': user,
                'password': password,
                'ssl_context': True  # Enable SSL for Neon
            }
            
            logger.info(f"✅ Render.com: Parsed database connection to {host}:{port}/{database} with SSL")
            
        except Exception as e:
            logger.error(f"❌ Render.com: Failed to parse DATABASE_URL: {e}")
    
    def get_db_connection(self):
        """Get a new database connection using pg8000."""
        try:
            conn = pg8000.native.Connection(**self.db_connection_params)
            return conn
        except Exception as e:
            logger.error(f"❌ Render.com: Database connection failed: {e}")
            return None
    
    def setup_flask_routes(self):
        """Setup Flask routes for health checks and monitoring."""
        
        @self.app.route('/health')
        def health_check():
            self.last_activity = time.time()
            uptime = time.time() - self.last_activity
            
            # Test database connection
            db_connected = False
            try:
                conn = self.get_db_connection()
                if conn:
                    conn.close()
                    db_connected = True
            except:
                pass
            
            status = {
                "status": "healthy",
                "service": "HVAC MQTT-Neon Bridge",
                "platform": "Render.com Free Tier (Flask + pg8000)",
                "mqtt_connected": self.connected,
                "database_connected": db_connected,
                "queue_size": len(self.message_queue),
                "uptime_seconds": uptime,
                "last_activity": datetime.fromtimestamp(self.last_activity).isoformat(),
                "timestamp": datetime.utcnow().isoformat(),
                "message": "Service is awake and processing HVAC data"
            }
            logger.info("🔔 Render.com: Health check - keeping service awake")
            return jsonify(status)
        
        @self.app.route('/stats')
        def stats():
            self.last_activity = time.time()
            stats_data = {
                "service": "HVAC MQTT-Neon Bridge",
                "version": "1.0.0",
                "platform": "Render.com Free Tier (Flask + pg8000)",
                "mqtt_broker": self.broker,
                "database": "Neon PostgreSQL (pg8000 driver)",
                "queue_size": len(self.message_queue),
                "batch_size": self.batch_size,
                "batch_timeout": self.batch_timeout,
                "uptime": time.time(),
                "status": "running" if self.running else "stopping",
                "connections": {
                    "mqtt": self.connected,
                    "database": self.db_connection_params is not None
                },
                "render_info": {
                    "free_tier": True,
                    "sleep_after_idle": "15 minutes",
                    "wake_time": "~30 seconds",
                    "framework": "Flask",
                    "database_driver": "pg8000 (pure Python)"
                }
            }
            return jsonify(stats_data)
        
        @self.app.route('/wake', methods=['GET', 'POST'])
        def wake_up():
            self.last_activity = time.time()
            logger.info("☀️ Render.com: Service woken up by external ping")
            
            # Reconnect MQTT if needed
            if not self.connected:
                logger.info("🔄 Render.com: Reconnecting MQTT after wake-up")
                self.connect_mqtt()
            
            return jsonify({
                "message": "Service is now awake",
                "timestamp": datetime.utcnow().isoformat(),
                "mqtt_connected": self.connected,
                "database_connected": self.db_connection_params is not None
            })
        
        @self.app.route('/')
        def root():
            return jsonify({
                "service": "HVAC MQTT-Neon Bridge",
                "platform": "Render.com Free Tier (Flask + pg8000)",
                "status": "running",
                "endpoints": {
                    "health": "/health",
                    "stats": "/stats", 
                    "wake": "/wake"
                },
                "message": "24/7 HVAC monitoring service running on Render.com"
            })
    
    def setup_mqtt(self):
        """Set up MQTT client with SSL and callbacks."""
        self.mqtt_client = mqtt.Client()
        self.mqtt_client.username_pw_set(self.mqtt_username, self.mqtt_password)
        
        # Configure SSL/TLS
        context = ssl.create_default_context(ssl.Purpose.SERVER_AUTH)
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        self.mqtt_client.tls_set_context(context)
        
        # Optimize for cold starts
        self.mqtt_client.reconnect_delay_set(min_delay=1, max_delay=10)
        self.mqtt_client.socket_timeout = 30
        self.mqtt_client.keepalive = 60
        
        # Set callbacks
        self.mqtt_client.on_connect = self.on_mqtt_connect
        self.mqtt_client.on_message = self._on_mqtt_message
        self.mqtt_client.on_disconnect = self._on_mqtt_disconnect
    
    def on_mqtt_connect(self, client, userdata, flags, rc):
        """MQTT connection callback."""
        if rc == 0:
            self.connected = True
            self.last_activity = time.time()
            logger.info("✅ Render.com: Connected to EMQX broker")
            
            # Subscribe to topics
            topics = [
                (f"{self.topic_prefix}/+/data", 1),
                (f"{self.topic_prefix}/+/heartbeat", 1)
            ]
            
            for topic, qos in topics:
                client.subscribe(topic, qos)
                logger.info(f"📡 Render.com: Subscribed to {topic}")
        else:
            logger.error(f"❌ Render.com: MQTT connection failed: {rc}")
            self.connected = False
    
    def _on_mqtt_message(self, client, userdata, msg):
        """Handle incoming MQTT messages."""
        try:
            self.last_activity = time.time()
            topic_parts = msg.topic.split('/')
            if len(topic_parts) >= 4:
                device_id = topic_parts[2]
                message_type = topic_parts[3]
                
                payload = json.loads(msg.payload.decode())
                
                logger.info(f"📨 Render.com: Received {message_type} from {device_id}")
                
                # Add to queue for batch processing
                self.message_queue.append({
                    'device_id': device_id,
                    'message_type': message_type,
                    'payload': payload,
                    'timestamp': datetime.utcnow()
                })
                
        except Exception as e:
            logger.error(f"❌ Render.com: Error processing message: {e}")
    
    def _on_mqtt_disconnect(self, client, userdata, rc):
        """MQTT disconnect callback."""
        self.connected = False
        logger.warning("📡 Render.com: Disconnected from MQTT broker")
    
    def _parse_timestamp(self, timestamp_value):
        """Parse various timestamp formats efficiently."""
        if isinstance(timestamp_value, str):
            try:
                return parser.isoparse(timestamp_value.replace('Z', '+00:00'))
            except:
                pass
        elif isinstance(timestamp_value, (int, float)):
            return datetime.fromtimestamp(timestamp_value)
        
        return datetime.utcnow()
    
    def _process_batch(self, messages):
        """Process messages in batches for Render efficiency."""
        if not self.db_connection_params or not messages:
            return
        
        conn = None
        try:
            conn = self.get_db_connection()
            if not conn:
                return
            
            for msg in messages:
                if msg['message_type'] == "data":
                    self._store_sensor_data(conn, msg)
                elif msg['message_type'] == "heartbeat":
                    self._update_device_status(conn, msg)
            
            logger.info(f"✅ Render.com: Processed batch of {len(messages)} messages")
            
        except Exception as e:
            logger.error(f"❌ Render.com: Error processing batch: {e}")
        finally:
            if conn:
                conn.close()
    
    def _store_sensor_data(self, conn, msg):
        """Store sensor data in Neon database."""
        device_id = msg['device_id']
        data = msg['payload']
        
        # Ensure device exists
        conn.run("""
            INSERT INTO "Device" (device_id, device_name, location, device_type)
            VALUES (:device_id, :device_name, :location, :device_type)
            ON CONFLICT (device_id) DO UPDATE SET
                last_seen = CURRENT_TIMESTAMP,
                updated_at = CURRENT_TIMESTAMP
        """, device_id=device_id, device_name=f"Device {device_id}", 
             location="Render-detected", device_type="HVAC_SENSOR")
        
        # Parse timestamp
        timestamp = self._parse_timestamp(data.get('timestamp'))
        raw_timestamp = str(data.get('timestamp', ''))
        
        # Insert sensor reading
        conn.run("""
            INSERT INTO "SensorReading" (
                device_id, timestamp, esp_timestamp_raw,
                ambient_temp, condenser_temp, evap_temp, supply_air_temp, return_air_temp,
                comp_current, fan_current, evap_fan_current, airflow_velocity, pressure,
                vibration_amp, vibration_freq, sound_level, dust_concentration, refrigerant_flow,
                dht22_humidity, bmp280_temperature, bmp280_pressure, bmp280_altitude
            ) VALUES (
                :device_id, :timestamp, :esp_timestamp_raw,
                :ambient_temp, :condenser_temp, :evap_temp, :supply_air_temp, :return_air_temp,
                :comp_current, :fan_current, :evap_fan_current, :airflow_velocity, :pressure,
                :vibration_amp, :vibration_freq, :sound_level, :dust_concentration, :refrigerant_flow,
                :dht22_humidity, :bmp280_temperature, :bmp280_pressure, :bmp280_altitude
            )
        """, 
            device_id=device_id, timestamp=timestamp, esp_timestamp_raw=raw_timestamp,
            ambient_temp=data.get('ambient_temp'), condenser_temp=data.get('condenser_temp'), 
            evap_temp=data.get('evap_temp'), supply_air_temp=data.get('supply_air_temp'), 
            return_air_temp=data.get('return_air_temp'), comp_current=data.get('comp_current'), 
            fan_current=data.get('fan_current'), evap_fan_current=data.get('evap_fan_current'),
            airflow_velocity=data.get('airflow_velocity'), pressure=data.get('pressure'),
            vibration_amp=data.get('vibration_amp'), vibration_freq=data.get('vibration_freq'), 
            sound_level=data.get('sound_level'), dust_concentration=data.get('dust_concentration'), 
            refrigerant_flow=data.get('refrigerant_flow'), dht22_humidity=data.get('dht22_humidity'), 
            bmp280_temperature=data.get('bmp280_temperature'), bmp280_pressure=data.get('bmp280_pressure'), 
            bmp280_altitude=data.get('bmp280_altitude')
        )
        
        # Check for alerts
        self._check_alerts(conn, device_id, data)
    
    def _update_device_status(self, conn, msg):
        """Update device status from heartbeat."""
        device_id = msg['device_id']
        
        conn.run("""
            INSERT INTO "Device" (device_id, device_name, location, device_type)
            VALUES (:device_id, :device_name, :location, :device_type)
            ON CONFLICT (device_id) DO UPDATE SET
                last_seen = CURRENT_TIMESTAMP,
                updated_at = CURRENT_TIMESTAMP,
                is_active = true
        """, device_id=device_id, device_name=f"Device {device_id}", 
             location="Render-detected", device_type="HVAC_SENSOR")
    
    def _check_alerts(self, conn, device_id, data):
        """Check for alert conditions and store in database."""
        alerts = []
        
        # Temperature alerts
        if data.get('ambient_temp', 0) > 35:
            alerts.append(("HIGH_TEMP", "CRITICAL", f"High ambient temperature: {data['ambient_temp']}°C", data['ambient_temp'], 35))
        
        if data.get('condenser_temp', 0) > 75:
            alerts.append(("HIGH_TEMP", "WARNING", f"High condenser temperature: {data['condenser_temp']}°C", data['condenser_temp'], 75))
        
        # Current alerts
        if data.get('comp_current', 0) > 30:
            alerts.append(("HIGH_CURRENT", "WARNING", f"High compressor current: {data['comp_current']}A", data['comp_current'], 30))
        
        # Vibration alerts
        if data.get('vibration_amp', 0) > 2.5:
            alerts.append(("HIGH_VIBRATION", "WARNING", f"High vibration: {data['vibration_amp']}mm", data['vibration_amp'], 2.5))
        
        # Insert alerts
        for alert_type, severity, message, value, threshold in alerts:
            conn.run("""
                INSERT INTO "SystemAlert" (device_id, alert_type, severity, message, value, threshold)
                VALUES (:device_id, :alert_type, :severity, :message, :value, :threshold)
            """, device_id=device_id, alert_type=alert_type, severity=severity, 
                 message=message, value=value, threshold=threshold)
            
            logger.warning(f"🚨 Render.com Alert: {severity} - {message}")
    
    def connect_mqtt(self):
        """Connect to MQTT broker."""
        try:
            logger.info(f"🔗 Render.com: Connecting to MQTT {self.broker}:{self.mqtt_port}")
            self.mqtt_client.connect(self.broker, self.mqtt_port, 60)
            self.mqtt_client.loop_start()
            return True
        except Exception as e:
            logger.error(f"❌ Render.com: MQTT connection failed: {e}")
            return False
    
    def run_bridge_task(self):
        """Run the bridge processing task in background thread."""
        last_batch_time = time.time()
        
        while self.running:
            current_time = time.time()
            
            # Process batch when queue is full or timeout reached
            if (len(self.message_queue) >= self.batch_size or 
                (self.message_queue and current_time - last_batch_time >= self.batch_timeout)):
                
                batch = self.message_queue[:self.batch_size]
                self.message_queue = self.message_queue[self.batch_size:]
                
                self._process_batch(batch)
                last_batch_time = current_time
            
            # Reconnect MQTT if disconnected
            if not self.connected and self.running:
                logger.info("🔄 Render.com: Reconnecting MQTT...")
                self.connect_mqtt()
                time.sleep(5)
            
            time.sleep(0.1)
    
    def run(self):
        """Run the Render.com bridge."""
        logger.info("🆓 Starting Render.com Free Tier MQTT-Neon Bridge (Flask + pg8000)")
        logger.info("=" * 60)
        
        # Check database connection
        if not self.db_connection_params:
            logger.error("❌ Render.com: Invalid database configuration")
            return False
        
        # Test database connection
        conn = self.get_db_connection()
        if conn:
            conn.close()
            logger.info("✅ Render.com: Connected to Neon database")
        else:
            logger.error("❌ Render.com: Database connection failed")
            return False
        
        # Connect to MQTT
        if not self.connect_mqtt():
            return False
        
        logger.info("🚀 Render.com: Bridge is running...")
        logger.info(f"🌐 Render.com: Flask server on port {self.port}")
        logger.info("📡 Render.com: Listening for MQTT messages...")
        logger.info("💾 Render.com: Storing data in Neon database...")
        logger.info("😴 Render.com: Will sleep after 15min idle (wakes on request)")
        
        # Start bridge processing task in background thread
        bridge_thread = threading.Thread(target=self.run_bridge_task, daemon=True)
        bridge_thread.start()
        
        logger.info(f"✅ Render.com: Flask server starting on port {self.port}")
        logger.info("💡 Render.com: Set up cron job to ping /health every 10min to prevent sleep")
        
        # Run Flask app
        self.app.run(host='0.0.0.0', port=self.port, debug=False)

    def keepalive_checker(self):
        """Background thread to send keep-alive requests every 5 minutes."""
        while True:
            try:
                current_time = time.time()
                if current_time - self.last_keepalive >= self.keepalive_interval:
                    logger.info("💓 Render.com: Sending keep-alive to prevent sleep")
                    # Self-ping to keep service awake
                    import requests
                    try:
                        requests.get(f"http://localhost:{self.port}/health", timeout=5)
                        self.last_keepalive = current_time
                    except:
                        # If local request fails, just update timestamp
                        self.last_keepalive = current_time
                
                time.sleep(60)  # Check every minute
            except Exception as e:
                logger.error(f"❌ Keep-alive error: {e}")
                time.sleep(60)

    def run_bridge(self):
        """Main bridge loop with batch processing."""
        logger.info("🚀 Render.com: Bridge is running...")
        logger.info("🌐 Render.com: Flask server on port 10000")
        logger.info("📡 Render.com: Listening for MQTT messages...")
        logger.info("💾 Render.com: Storing data in Neon database...")
        logger.info("💓 Render.com: Auto keep-alive every 5 minutes")
        logger.info("😴 Render.com: Will sleep after 15min idle (wakes on request)")
        
        # Start background batch processor
        batch_thread = threading.Thread(target=self.batch_processor, daemon=True)
        batch_thread.start()
        
        # Start keep-alive thread
        keepalive_thread = threading.Thread(target=self.keepalive_checker, daemon=True)
        keepalive_thread.start()
        
        # Connect to MQTT
        self.connect_mqtt()
        
        # Start Flask server
        logger.info("✅ Render.com: Flask server starting on port 10000")
        logger.info("💡 Render.com: Auto keep-alive prevents sleep - no manual pinging needed")
        self.app.run(host='0.0.0.0', port=self.port, debug=False)

# Create bridge instance
bridge = RenderPG8000MQTTBridge()

if __name__ == "__main__":
    bridge.run_bridge()
