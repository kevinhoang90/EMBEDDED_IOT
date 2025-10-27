package com.example.smartdoor

import android.content.Intent
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.graphics.drawable.DrawableCompat
import com.example.smartdoor.databinding.ActivityMainBinding
import com.google.firebase.database.*

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding
    private lateinit var db: DatabaseReference
    private var doorOpen = false
    private var lastUser = "none"

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        db = FirebaseDatabase.getInstance().reference

        listenDoorState()
        listenNewCard()

        binding.btnToggleDoor.setOnClickListener { sendToggleCommand() }
        binding.btnAddCard.setOnClickListener { openAddCardScreen() } // ✅ chỉ mở giao diện
    }

    /** 🔸 Lắng nghe trạng thái cửa và người dùng */
    private fun listenDoorState() {
        db.addValueEventListener(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                if (!snapshot.exists()) return

                val door = snapshot.child("door").getValue(Int::class.java) ?: 0
                var user = snapshot.child("user").getValue(String::class.java) ?: "none"
                val time = snapshot.child("time").getValue(String::class.java) ?: "--:--"

                // ✅ nếu user là "ADMIN" hoặc "XXXX" thì hiển thị Admin
                if (user == "ADMIN" || user == "XXXX") user = "Admin"
                if (user == "none") user = lastUser

                doorOpen = (door == 1)
                lastUser = user

                renderDoorState(user, time)
            }

            override fun onCancelled(error: DatabaseError) {
                Toast.makeText(
                    this@MainActivity,
                    "Firebase error: ${error.message}",
                    Toast.LENGTH_SHORT
                ).show()
            }
        })
    }

    /** 🔸 Lắng nghe khi có thẻ mới */
    private fun listenNewCard() {
        db.child("new_card").addValueEventListener(object : ValueEventListener {
            override fun onDataChange(snapshot: DataSnapshot) {
                val cardId = snapshot.getValue(String::class.java) ?: "none"
                if (cardId != "none" && cardId != "XXXX") {
                    Toast.makeText(
                        this@MainActivity,
                        "📇 Thẻ mới: $cardId",
                        Toast.LENGTH_LONG
                    ).show()
                    binding.tvLastEventName.text = "Thẻ mới: $cardId"
                    db.child("new_card").setValue("none")
                }
            }

            override fun onCancelled(error: DatabaseError) {}
        })
    }

    /** 🔸 Gửi lệnh mở cửa */
    private fun sendToggleCommand() {
        db.child("cmd_open").setValue("toggle")
            .addOnSuccessListener {
                Toast.makeText(this, "✅ Gửi lệnh mở/đóng cửa", Toast.LENGTH_SHORT).show()
                db.child("user").setValue("ADMIN")
            }
            .addOnFailureListener {
                Toast.makeText(this, "❌ Lỗi: ${it.message}", Toast.LENGTH_SHORT).show()
            }
    }

    /** 🔸 Chỉ mở màn hình AddCardActivity (không gửi lệnh add ở đây) */
    private fun openAddCardScreen() {
        val intent = Intent(this, AddCardActivity::class.java)
        startActivity(intent)
    }

    /** 🔸 Cập nhật giao diện trạng thái cửa */
    private fun renderDoorState(userId: String, time: String) {
        val stateText = if (doorOpen) "OPEN" else "CLOSED"
        binding.tvDoorState.text = "Trạng thái: $stateText"

        val color = ContextCompat.getColor(
            this,
            if (doorOpen) R.color.statusOpen else R.color.statusClosed
        )
        val bg = binding.viewStatus.background.mutate()
        DrawableCompat.setTint(bg, color)
        binding.viewStatus.background = bg
        binding.tvDoorState.setTextColor(color)

        binding.tvLastEventName.text = "Người mở gần nhất: $userId"
        binding.tvLastEventId.text = "ID: $userId"
        binding.tvLastEventTime.text = "Lúc: $time"
        binding.btnToggleDoor.text = if (doorOpen) "Đóng cửa" else "Mở cửa"
    }
}
