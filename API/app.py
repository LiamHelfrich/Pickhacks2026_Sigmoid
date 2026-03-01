from flask import Flask, jsonify, request
from birdnet import analyze_recording
from pymongo import MongoClient
from bson import ObjectId
from flask_cors import CORS
# from diffusers import StableDiffusion3Pipeline
# import torch
import time
import io
import os
import random

# Point all HuggingFace/diffusers cache to storage partition
# os.environ.setdefault("HF_HOME", "/mnt/big/huggingface")

mongrel_client = MongoClient("mongodb://localhost:27017/")
db = mongrel_client['maindb']
col = db['data']
print('Connected to the mongrel dog server!')

# --- Load SD3 pipeline once at startup ---
# print("Loading Stable Diffusion 3 pipeline...")
# SD3_MODEL = "stabilityai/stable-diffusion-3-medium-diffusers"

# pipe = StableDiffusion3Pipeline.from_pretrained(
#     SD3_MODEL,
#     torch_dtype=torch.float16,        # float16 saves VRAM on most VPS GPUs
#     use_safetensors=True,
#     cache_dir="/mnt/big/huggingface/hub"
# )
# pipe = pipe.to("cuda")
# pipe.enable_model_cpu_offload()       # offload unused components to RAM to save VRAM
# print("SD3 pipeline ready.")


def generate_bird_image(common_name: str, scientific_name: str) -> bytes:
    """Generate a cartoon bird image and return it as PNG bytes."""
    prompt = (
        f"A cute cartoon illustration of a {common_name} ({scientific_name}), "
        "colorful digital art, children's book style, white background, "
        "high quality, detailed feathers, friendly expression"
    )
    negative_prompt = (
        "photorealistic, photograph, dark, scary, blurry, low quality, "
        "watermark, text, multiple birds"
    )

    image = pipe(
        prompt=prompt,
        negative_prompt=negative_prompt,
        num_inference_steps=28,
        guidance_scale=7.0,
        height=512,
        width=512,
    ).images[0]

    buf = io.BytesIO()
    image.save(buf, format="PNG")
    return buf.getvalue()


def pick_best_detection(detections: list) -> dict | None:
    """Return the detection with the highest confidence, or None if empty."""
    if not detections:
        return None
    return max(detections, key=lambda d: d.get("confidence", 0))


app = Flask(__name__)


CORS(app, resources={r"/*": {"origins": "*"}}, expose_headers=["Content-Range", "Accept-Ranges", "Content-Length"])


@app.post("/upload")
def upload_binary_blob():
    blob = request.get_data(cache=False, as_text=False)

    if not blob:
        return jsonify({"error": "No binary data received"}), 400

    print("Detecting...")
    detections, mp3_bytes = analyze_recording(blob)
    print(f"Detections: {detections}")

    # Generate a cartoon image for the most confident detection
    bird_image_png = None
    # best = pick_best_detection(detections)
    # if best:
    #     print(f"Generating cartoon for: {best['common_name']} ({best['scientific_name']})")
    #     try:
    #         bird_image_png = generate_bird_image(
    #             best["common_name"],
    #             best["scientific_name"]
    #         )
    #         print(f"Image generated: {len(bird_image_png)} bytes")
    #     except Exception as e:
    #         print(f"Image generation failed: {e}")

    doc = {
        "mp3_data": mp3_bytes,
        "detections": detections,
        "time": int(time.time()),
        "bird_image_png": bird_image_png,          # None if generation failed/no detections
        "lat": 37.9549007 + random.uniform(0.0002,-0.0002),
        "lon": -91.776418 + random.uniform(0.0002,-0.0002)
        # "generated_for": best["common_name"] if best else None,
    }

    if detections != []:
        result_db = col.insert_one(doc)
        print(f'The mongrel dog responds: {result_db}')
    else:
        print('Nothing to report, sir!')

    return jsonify(
        {
            "message": "Binary blob received",
            "bytes_received": len(blob),
            "content_type": request.content_type,
            "detections_count": len(detections),
        }
    ), 200

@app.get("/detections/<doc_id>")
def get_detection_by_id(doc_id):
    doc = col.find_one({"_id": ObjectId(doc_id)}, {"_id": 0, "detections": 1, "time": 1, "lat": 1, "lon": 1})
    if doc is None:
        return jsonify({"error": "Document not found"}), 404
    return jsonify(doc)

from flask import send_file
import io


@app.get("/detections/<doc_id>/audio")
def get_detection_audio(doc_id):
    try:
        oid = ObjectId(doc_id)
    except Exception:
        return jsonify({"error": "Invalid document id"}), 400

    doc = col.find_one({"_id": oid}, {"mp3_data": 1})
    if not doc or not doc.get("mp3_data"):
        return jsonify({"error": "Audio not found"}), 404

    mp3_bytes = bytes(doc["mp3_data"])
    bio = io.BytesIO(mp3_bytes)
    bio.seek(0)

    resp = send_file(
        bio,
        mimetype="audio/mpeg",
        as_attachment=False,
        download_name=f"{doc_id}.mp3",
        conditional=True,   # ✅ enables Range requests -> 206
        max_age=0
    )

    # Explicitly advertise range support (usually added automatically)
    resp.headers["Accept-Ranges"] = "bytes"
    return resp

@app.get("/uids")
def get_uids():
    docs = col.find({}, {"_id": 1})
    return jsonify([str(doc["_id"]) for doc in docs])

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True, use_reloader=False)