#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "string.h"
#include "tim.h"
#include "RC522.h"
#include <stdio.h>

typedef enum {
  ST_INIT = 0,
  ST_IDLE,
  ST_CHECK_AUTH,
  ST_OPEN_DOOR_ID,
  ST_OPEN_DOOR_ADMIN,
  ST_CLOSE_DOOR,
  ST_ADD_CARD,
  ST_POST_ADD_CARD
} app_state_t;

#define LED_GPIO_Port    GPIOC
#define LED_Pin          GPIO_PIN_13
#define OPEN_TIMEOUT_MS  5000

#define AUTH_MAX 16
static uint8_t auth_ids[AUTH_MAX][4];
static uint8_t auth_count = 0;

static app_state_t state = ST_INIT;
static uint8_t uid[5];
static uint8_t last_uid4[4];
static uint8_t last_detected_uid[4] = {0};
static uint8_t have_uid = 0;
static uint8_t rx_byte;
static char    rx_line[32];
static uint8_t rx_idx = 0;
static uint32_t t_open_start = 0;
static uint8_t addcard_mode = 0;


static void door_open(void)  {
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1000);
}
static void door_close(void) {
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 2000);
}

static void uid_to_hex8(const uint8_t* u4, char* out8) {
  for (int i=0;i<4;i++) sprintf(&out8[i*2], "%02X", u4[i]);
  out8[8]='\0';
}
static uint8_t uid_in_auth(const uint8_t* u4){
  for(uint8_t i=0;i<auth_count;i++)
    if(memcmp(auth_ids[i],u4,4)==0) return 1;
  return 0;
}
static void auth_add_uid(const uint8_t* u4){
  if(auth_count>=AUTH_MAX) return;
  if(uid_in_auth(u4)) return;
  memcpy(auth_ids[auth_count++],u4,4);
}
static void send_packet(const char* code, const char* id){
  char msg[32];
  if(id) snprintf(msg,sizeof(msg),"%s,%s\n",code,id);
  else   snprintf(msg,sizeof(msg),"%s\n",code);
  HAL_UART_Transmit(&huart1,(uint8_t*)msg,strlen(msg),100);
  HAL_Delay(60);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart){
  if(huart->Instance==USART1){
    char c=(char)rx_byte;
    if(c=='\n'||c=='\r'){
      rx_line[rx_idx]='\0'; rx_idx=0;
      if(strcmp(rx_line,"O")==0) state=ST_OPEN_DOOR_ADMIN;
      else if(strcmp(rx_line,"R")==0){ addcard_mode=1; state=ST_ADD_CARD; }
      memset(rx_line,0,sizeof(rx_line));
    } else if(rx_idx<sizeof(rx_line)-1) rx_line[rx_idx++]=c;
    HAL_UART_Receive_IT(&huart1,&rx_byte,1);
  }
}


static uint8_t rfid_read_once(uint8_t out4[4]){
  uint8_t tmp[MAX_LEN];
  if(MFRC522_Request(PICC_REQIDL,tmp)!=MI_OK) return 0;
  if(MFRC522_Anticoll(tmp)!=MI_OK) return 0;
  if(memcmp(tmp,last_detected_uid,4)==0) { MFRC522_Halt(); return 0; }
  memcpy(out4,tmp,4);
  memcpy(last_detected_uid,tmp,4);
  MFRC522_Halt();
  return 1;
}

// --- Main ---
void SystemClock_Config(void);

int main(void){
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  HAL_UART_Receive_IT(&huart1,&rx_byte,1);
  MFRC522_Init();
  MX_TIM1_Init();

  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 1000);

  door_close();
  state=ST_IDLE;

  while(1){
    switch(state){
      case ST_IDLE:{
        if(rfid_read_once(uid)){
          memcpy(last_uid4,uid,4);
          have_uid=1;
          state=ST_CHECK_AUTH;
        }
        HAL_Delay(40);
        break;
      }

      case ST_CHECK_AUTH:{
        if(!have_uid){ state=ST_IDLE; break; }
        if(uid_in_auth(last_uid4)) state=ST_OPEN_DOOR_ID;
        else state=ST_IDLE;
        break;
      }

      case ST_OPEN_DOOR_ID:{
        char idhex[9]; uid_to_hex8(last_uid4,idhex);
        door_open();
        send_packet("ODI",idhex);
        t_open_start=HAL_GetTick();
        state=ST_CLOSE_DOOR;
        break;
      }

      case ST_OPEN_DOOR_ADMIN:{
        door_open();
        send_packet("ODA","A");
        t_open_start=HAL_GetTick();
        state=ST_CLOSE_DOOR;
        break;
      }

      case ST_CLOSE_DOOR:{
        if(HAL_GetTick()-t_open_start>=OPEN_TIMEOUT_MS){
          door_close();
          char idhex[9]="XXXX";
          if(have_uid) uid_to_hex8(last_uid4,idhex);
          send_packet("CD",idhex);
          have_uid=0;
          memset(last_detected_uid,0,sizeof(last_detected_uid));
          state=ST_IDLE;
        }
        HAL_Delay(10);
        break;
      }

      case ST_ADD_CARD: {
          uint32_t t0 = HAL_GetTick();
          uint8_t newu[4];
          uint8_t got = 0;

          memset(last_detected_uid, 0, sizeof(last_detected_uid));
          HAL_Delay(100);

          while (HAL_GetTick() - t0 < 8000) {
              if (rfid_read_once(newu)) {
                  got = 1;
                  break;
              }
              HAL_Delay(80);
          }

          if (got) {
              char idhex[9];
              uid_to_hex8(newu, idhex);
              auth_add_uid(newu);

              // ✅ Gửi thẻ hợp lệ
              send_packet("AC", idhex);
              HAL_Delay(100);
          } else {
              send_packet("AC", "XXXX");
              HAL_Delay(100);
          }

          addcard_mode = 0;
          have_uid = 0;
          memset(last_detected_uid, 0, sizeof(last_detected_uid));
          state = ST_POST_ADD_CARD;
          break;
      }

      case ST_POST_ADD_CARD: {
    	  HAL_Delay(400);
      }

      default: state=ST_IDLE; break;
    }
  }
}



void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 84;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
