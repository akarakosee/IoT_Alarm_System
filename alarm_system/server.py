from flask import Flask, request, jsonify
import cv2
from PIL import Image
import io
import numpy as np
import os
from datetime import datetime
import torch

app = Flask(__name__)

#########################
# 1) HAAR CASCADE SETUP #
#########################
cascade_path = cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'
face_cascade = cv2.CascadeClassifier(cascade_path)

#########################
# 2) YOLOv5 MODEL LOAD  #
#########################
model = torch.hub.load('ultralytics/yolov5', 'yolov5s', pretrained=True)
model_classes = model.names  # Örneğin: ['person','bicycle','car',...]

@app.route('/detect', methods=['POST'])
def detect_face_and_yolo():
    if 'image' not in request.files:
        print("No image part in the request")
        return jsonify({'error': 'No image part'}), 400

    file = request.files['image']
    if file.filename == '':
        print("No selected file")
        return jsonify({'error': 'No selected file'}), 400

    try:
        # (A) Görüntüyü Pillow ile aç
        img = Image.open(io.BytesIO(file.read()))
        img = img.convert('RGB')
        img_array = np.array(img)
        # (B) OpenCV BGR formatına çevir
        img_bgr = cv2.cvtColor(img_array, cv2.COLOR_RGB2BGR)

        ############################################
        # 3) HAAR CASCADE ile YÜZ ALGILAMA
        ############################################
        gray = cv2.cvtColor(img_array, cv2.COLOR_RGB2GRAY)
        faces = face_cascade.detectMultiScale(
            gray,
            scaleFactor=1.05,  # Daha hassas tarama
            minNeighbors=3,
            minSize=(30, 30)
        )
        face_count = len(faces)

        ############################################
        # 4) YOLOv5 İLE ALGILAMA
        ############################################
        # Görüntüyü YOLOv5 ile işleyelim
        results = model(img_bgr)
        detections = results.xyxy[0]  # [x1, y1, x2, y2, conf, class]
        yolo_detections = []

        person_detected = False  # YOLO "person" var mı diye bakacağız

        for det in detections:
            x1, y1, x2, y2, conf, cls_id = det
            x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
            cls_id = int(cls_id)
            cls_name = model_classes[cls_id]
            yolo_detections.append({
                'class': cls_name,
                'confidence': float(conf),
                'bbox': [x1, y1, x2, y2]
            })

            # Eğer 'person' sınıfı varsa işaretle
            if cls_name.lower() == 'person':
                person_detected = True

        # face_detected = True/False
        face_detected = (face_count > 0)

        # Hem Haar Cascade yüz, hem de YOLO "person" var mı?
        # En az birisi varsa 'detected' = true diyelim
        detected = face_detected or person_detected

        ############################################
        # 5) GÖRÜNTÜYÜ KAYDETME (SADECE DETECTED = TRUE İSE)
        ############################################
        if detected:
            # Klasör yoksa oluştur
            if not os.path.exists('captured_images'):
                os.makedirs('captured_images')
            timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
            save_path = os.path.join('captured_images', f'image_{timestamp}.jpg')
            img.save(save_path)
            print(f"[INFO] Image saved to {save_path}")

        ############################################
        # 6) JSON YANITINI DÖNDÜRME
        ############################################
        return jsonify({
            'faces': face_count,             # Haar Cascade yüz sayısı
            'yolo_detections': yolo_detections, 
            'detected': detected            # True/False
        }), 200

    except Exception as e:
        print(f"Error processing image: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    # Eğer yoksa klasörü oluştur
    if not os.path.exists('captured_images'):
        os.makedirs('captured_images')

    app.run(host='0.0.0.0', port=5000)
