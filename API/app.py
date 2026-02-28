from flask import Flask, jsonify, request

app = Flask(__name__)


@app.post("/upload")
def upload_binary_blob():
    blob = request.get_data(cache=False, as_text=False)

    if not blob:
        return jsonify({"error": "No binary data received"}), 400

    return jsonify(
        {
            "message": "Binary blob received",
            "bytes_received": len(blob),
            "content_type": request.content_type,
        }
    ), 200


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000, debug=True)
