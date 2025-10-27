package com.example.smartdoor

import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.example.smartdoor.databinding.ActivityAddCardBinding
import com.google.firebase.database.*

class AddCardActivity : AppCompatActivity() {

    private lateinit var binding: ActivityAddCardBinding
    private lateinit var db: DatabaseReference
    private var listener: ValueEventListener? = null
    private val uiHandler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityAddCardBinding.inflate(layoutInflater)
        setContentView(binding.root)

        db = FirebaseDatabase.getInstance().reference

        binding.btnStartEnroll.setOnClickListener { startEnroll() }
        binding.btnCancel.setOnClickListener { finish() } // nút hủy quay lại
    }

    /** 🔸 Gửi lệnh yêu cầu STM32 thêm thẻ */
    private fun startEnroll() {
        binding.tvStatus.text = "⏳ Đang chờ quét thẻ..."
        binding.tvCardId.text = "Card ID: --"

        // Gửi lệnh đến ESP32/STM32
        db.child("cmd_add").setValue("add")
        db.child("user").setValue("ADMIN")

        // Hủy listener cũ nếu có
        listener?.let { db.child("new_card").removeEventListener(it) }

        // Lắng nghe thẻ mới
        listener = object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val cardId = snapshot.getValue(String::class.java) ?: "none"
                if (cardId == "none") return

                if (cardId == "XXXX") {
                    // ❌ Lỗi đọc thẻ
                    binding.tvStatus.text = "❌ Không đọc được thẻ, vui lòng thử lại!"
                    Toast.makeText(
                        this@AddCardActivity,
                        "❌ Không đọc được thẻ!",
                        Toast.LENGTH_SHORT
                    ).show()
                    db.child("new_card").setValue("none")
                } else {
                    // ✅ Thẻ hợp lệ
                    binding.tvCardId.text = "Card ID: $cardId"
                    binding.tvStatus.text = "✅ Đọc thẻ thành công!"
                    Toast.makeText(
                        this@AddCardActivity,
                        "✅ Đã nhận thẻ: $cardId",
                        Toast.LENGTH_SHORT
                    ).show()

                    // Reset Firebase flags
                    db.child("cmd_add").setValue("none")
                    db.child("new_card").setValue("none")
                    db.child("user").setValue("none")

                    // Trở về màn hình chính sau 1.5s
                    uiHandler.postDelayed({
                        finish()
                    }, 1500)
                }
            }

            override fun onCancelled(error: DatabaseError) {}
        }

        db.child("new_card").addValueEventListener(listener as ValueEventListener)
    }

    override fun onDestroy() {
        super.onDestroy()
        listener?.let { db.child("new_card").removeEventListener(it) }
    }
}
